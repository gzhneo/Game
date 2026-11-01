/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineTests.cpp -- tests for the engine building blocks under Code/Core/EngineCore. No framework,
// like Code/Tests/UnitTests.
//
// The tested files depend on no engine header (no PreRTS.h, no singletons) so this target builds
// in seconds and also compiles outside Windows -- the job system can be tested without game
// assets.

#include "Engine/BigArchive.h"
#include "Engine/Clock.h"
#include "Engine/CpuTopology.h"
#include "Engine/EngineLog.h"
#include "Engine/FormatInto.h"
#include "Engine/WideFormatCheck.h"
#include "Engine/GenerationTable.h"
#include "Engine/JobSystem.h"
#include "GameLogic/AICombatModel.h"
#include "Engine/LogicAccessGuard.h"
#include "Engine/NetAddress.h"
#include "Engine/NetAddressBook.h"
#include "Engine/NetEmulator.h"
#include "Engine/PacketSeal.h"
#include "Engine/RateLimiter.h"
#include "Engine/PortMapping.h"
#include "Engine/PreferenceFile.h"
#include "Engine/Settings.h"
#include "Engine/WheelSteps.h"
#include "Engine/ReplayCrcQueue.h"
#include "Engine/ScrollTiming.h"
#include "Engine/KeyLayout.h"
#include "Engine/InputQueue.h"
#include "Engine/RunAhead.h"
#include "Engine/WatchStream.h"
#include "Engine/Wire.h"
#include "Engine/SessionMessage.h"
#include "Engine/SessionHandshake.h"
#include "Engine/InviteCode.h"
#include "Engine/Rendezvous.h"
#include "Engine/RendezvousServer.h"

#include <cmath>
#include "Engine/ScopedGlobal.h"
#include "Engine/EngineContext.h"
#include "Engine/SimRandom.h"
#include "Engine/SimThread.h"
#include "Engine/TextFiles.h"
#include "Engine/TextureTier.h"
#include "Engine/TexturePackage.h"
#include "Engine/UserDataFiles.h"
#include "Engine/Vfs.h"
#include "Engine/ZipArchive.h"
#include "Engine/ThreadRole.h"
#include "Engine/Watchdog.h"
#include "Engine/WndFields.h"
#include "Engine/ModCatalog.h"
#include "Engine/PeerLink.h"
#include "Engine/PositionalFile.h"
#include "Engine/RecentIds.h"
#include "Engine/RemovalVote.h"
#include "Engine/LocalNetworks.h"
#include "Engine/RendezvousServerList.h"
#include "GameClient/KeyDefs.h"		// key codes without dinput.h
#include "GameClient/KeyScanCode.h"
#include "Monocypher/monocypher.h"
#include <version>
#ifdef __cpp_lib_format
#include "formatinto.h"
#endif

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

#include <zlib.h>

static int s_failures = 0;
static int s_checks = 0;

static void check(bool condition, const char *what)
{
	++s_checks;
	if (!condition)
	{
		++s_failures;
		std::printf("  FAILED: %s\n", what);
	}
}

#define CHECK(cond) check((cond), #cond)

// Like CHECK, but does not count every loop iteration (only failures are reported).
static void checkSilent(bool condition, const char *what)
{
	if (!condition)
	{
		++s_failures;
		std::printf("  FAILED: %s\n", what);
	}
}
#define CHECK_SILENT(cond) checkSilent((cond), #cond)

// ------------------------------------------------------------------ Clock
static void testClock()
{
	std::printf("Clock\n");

	const std::uint64_t t0 = Engine::Clock::microseconds();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	const std::uint64_t t1 = Engine::Clock::microseconds();
	CHECK(t1 > t0);
	CHECK((t1 - t0) >= 3000);			// at least 3 ms passed (sleep accuracy)
	CHECK((t1 - t0) < 2000000);			// and certainly not 2 seconds

	// Milliseconds and seconds must match the microsecond base
	const std::uint64_t us = Engine::Clock::microseconds();
	const std::uint32_t ms = Engine::Clock::milliseconds();
	CHECK(ms >= static_cast<std::uint32_t>(us / 1000u));
	CHECK(ms - static_cast<std::uint32_t>(us / 1000u) < 50);

	Engine::ScopedTimer timer;
	std::this_thread::sleep_for(std::chrono::milliseconds(2));
	CHECK(timer.elapsedMilliseconds() >= 1.0);

	// Logic time: pure frame arithmetic, no real time
	CHECK(Engine::LogicTime::FRAMES_PER_SECOND == 30);
	CHECK(Engine::LogicTime::framesFromSeconds(1.0) == 30);
	CHECK(Engine::LogicTime::framesFromSeconds(0.5) == 15);
	CHECK(Engine::LogicTime::framesFromMilliseconds(1000) == 30);
	CHECK(Engine::LogicTime::framesFromMilliseconds(100) == 3);
	CHECK(Engine::LogicTime::millisecondsFromFrames(30) == 1000);
	CHECK(Engine::LogicTime::secondsFromFrames(60) > 1.99 && Engine::LogicTime::secondsFromFrames(60) < 2.01);
}

// ------------------------------------------------------------ Thread roles
static void testThreadRole()
{
	std::printf("ThreadRole\n");

	Engine::setThreadRole(Engine::THREAD_ROLE_MAIN_AND_SIM, "Test-Hauptthread");
	CHECK(Engine::threadRole() == Engine::THREAD_ROLE_MAIN_AND_SIM);
	// A combined main/sim thread carries both roles
	CHECK(Engine::threadHasRole(Engine::THREAD_ROLE_MAIN));
	CHECK(Engine::threadHasRole(Engine::THREAD_ROLE_SIM));
	CHECK(!Engine::threadHasRole(Engine::THREAD_ROLE_WORKER));
	CHECK(!Engine::threadIsWorker());

	// The role is thread-local: another thread does not inherit it
	Engine::ThreadRole roleInOtherThread = Engine::THREAD_ROLE_MAIN;
	bool workerSeesWorker = false;
	std::thread other([&]() {
		roleInOtherThread = Engine::threadRole();
		Engine::setThreadRole(Engine::THREAD_ROLE_WORKER, "Test-Worker");
		workerSeesWorker = Engine::threadIsWorker() && !Engine::threadHasRole(Engine::THREAD_ROLE_SIM);
	});
	other.join();
	CHECK(roleInOtherThread == Engine::THREAD_ROLE_UNKNOWN);
	CHECK(workerSeesWorker);
	// and the own role is unchanged
	CHECK(Engine::threadRole() == Engine::THREAD_ROLE_MAIN_AND_SIM);

	// Violations are reported only once per site
	CHECK(Engine::reportThreadRoleViolation("Test.cpp", 4711, "Sim", "testThreadRole"));
	CHECK(!Engine::reportThreadRoleViolation("Test.cpp", 4711, "Sim", "testThreadRole"));
	CHECK(Engine::reportThreadRoleViolation("Test.cpp", 4712, "Sim", "testThreadRole"));
	CHECK(Engine::threadRoleViolationSiteCount() == 2);

	// The list counts every call per site, most frequent first
	const char *path = "enginetests-roles.txt";
	std::remove(path);
	CHECK(Engine::writeThreadRoleViolationsTo(path));
	std::FILE *f = std::fopen(path, "r");
	CHECK(f != nullptr);
	bool sawHeader = false, firstIsTwice = false;
	if (f != nullptr)
	{
		char line[512];
		int row = 0;
		while (std::fgets(line, sizeof(line), f) != nullptr)
		{
			if (std::strstr(line, "2 sites, 3 calls") != nullptr)
				sawHeader = true;
			if (std::strstr(line, "Test.cpp:") != nullptr && row++ == 0)
				firstIsTwice = std::strncmp(line, "2 ", 2) == 0 && std::strstr(line, ":4711") != nullptr;
		}
		std::fclose(f);
	}
	CHECK(sawHeader);
	CHECK(firstIsTwice);
	std::remove(path);
	Engine::clearThreadRoleViolations();
	CHECK(Engine::threadRoleViolationSiteCount() == 0);
	CHECK(Engine::reportThreadRoleViolation("Test.cpp", 4711, "Sim", "testThreadRole"));	// reported again after clearing
	Engine::clearThreadRoleViolations();

	CHECK(std::strcmp(Engine::threadRoleName(Engine::THREAD_ROLE_SIM), "Sim") == 0);
}

// ----------------------------------------------------------------- Logging
static void testLogLevels()
{
	std::printf("EngineLog levels and categories\n");

	Engine::Log::setMinLevel(Engine::LOG_TRACE);
	CHECK(Engine::Log::isEnabled(Engine::LOG_CAT_LOGIC, Engine::LOG_TRACE));

	Engine::Log::setMinLevel(Engine::LOG_WARNING);
	CHECK(!Engine::Log::isEnabled(Engine::LOG_CAT_LOGIC, Engine::LOG_INFO));
	CHECK(Engine::Log::isEnabled(Engine::LOG_CAT_LOGIC, Engine::LOG_ERROR));

	// A category can be stricter than the global level, never looser
	Engine::Log::setMinLevel(Engine::LOG_TRACE);
	Engine::Log::setCategoryMinLevel(Engine::LOG_CAT_AUDIO, Engine::LOG_ERROR);
	CHECK(!Engine::Log::isEnabled(Engine::LOG_CAT_AUDIO, Engine::LOG_INFO));
	CHECK(Engine::Log::isEnabled(Engine::LOG_CAT_AUDIO, Engine::LOG_ERROR));
	CHECK(Engine::Log::isEnabled(Engine::LOG_CAT_LOGIC, Engine::LOG_INFO));
	Engine::Log::setCategoryMinLevel(Engine::LOG_CAT_AUDIO, Engine::LOG_TRACE);

	// A filtered line must not end up in the ring buffer
	Engine::Log::setMinLevel(Engine::LOG_ERROR);
	const std::size_t before = Engine::Log::ringCount();
	ENGINE_LOG_INFO(Engine::LOG_CAT_LOGIC, "this line is filtered");
	CHECK(Engine::Log::ringCount() == before);
	Engine::Log::setMinLevel(Engine::LOG_TRACE);
	ENGINE_LOG_INFO(Engine::LOG_CAT_LOGIC, "this line is not");
	CHECK(Engine::Log::ringCount() == before + 1);
}

static void testLogRingBuffer()
{
	std::printf("EngineLog ring buffer\n");

	Engine::Log::setMinLevel(Engine::LOG_TRACE);
	// Write more lines than the ring buffer holds: it keeps the last RING_CAPACITY
	const std::size_t extra = 250;
	for (std::size_t i = 0; i < Engine::Log::RING_CAPACITY + extra; ++i)
		Engine::Log::write(Engine::LOG_CAT_ENGINE, Engine::LOG_INFO, "Ringtest %u", static_cast<unsigned>(i));

	CHECK(Engine::Log::ringCount() == Engine::Log::RING_CAPACITY);

	Engine::LogRecord first, last;
	CHECK(Engine::Log::ringRecord(0, first));
	CHECK(Engine::Log::ringRecord(Engine::Log::RING_CAPACITY - 1, last));
	// The newest entry is the last written line ...
	char expectedLast[64];
	std::snprintf(expectedLast, sizeof(expectedLast), "Ringtest %u",
		static_cast<unsigned>(Engine::Log::RING_CAPACITY + extra - 1));
	CHECK(std::strcmp(last.text, expectedLast) == 0);
	// ... and the oldest is exactly RING_CAPACITY-1 lines before it (order is correct)
	CHECK(std::strncmp(first.text, "Ringtest ", 9) == 0);
	CHECK(Engine::Log::ringRecord(Engine::Log::RING_CAPACITY, last) == false);
}

static void testLogRingBufferSnapshot()
{
	std::printf("EngineLog crash snapshot while other threads log\n");

	// writeRingBufferTo() runs in the crash handler, while other threads keep logging. Copying
	// entry by entry under separate locks would let the ring start move between entries, and the
	// dump would contain gaps and duplicates.
	Engine::Log::setMinLevel(Engine::LOG_TRACE);

	std::atomic<bool> stop{false};
	std::atomic<unsigned> counter{0};
	std::thread writer([&]() {
		while (!stop.load(std::memory_order_relaxed))
			Engine::Log::write(Engine::LOG_CAT_ENGINE, Engine::LOG_INFO, "Snapshot %u",
				counter.fetch_add(1, std::memory_order_relaxed));
	});

	// wait until the ring has filled once -- otherwise we test against a half-filled ring
	while (counter.load(std::memory_order_relaxed) < Engine::Log::RING_CAPACITY * 2)
		std::this_thread::yield();

	const char *dumpPath = "EngineLogDump.txt";
	CHECK(Engine::Log::writeRingBufferTo(dumpPath));
	stop.store(true);
	writer.join();

	std::FILE *f = std::fopen(dumpPath, "r");
	CHECK(f != nullptr);
	if (f == nullptr)
		return;

	// The writer counts up without gaps, so the numbers in the dump must do the same.
	long long previous = -1;
	std::size_t lines = 0;
	bool contiguous = true;
	char line[512];
	while (std::fgets(line, sizeof(line), f) != nullptr)
	{
		const char *marker = std::strstr(line, "Snapshot ");
		if (marker == nullptr)
			continue;	// lines from earlier tests are still in the ring
		const long long value = std::atoll(marker + 9);
		if (previous >= 0 && value != previous + 1)
			contiguous = false;
		previous = value;
		++lines;
	}
	std::fclose(f);
	std::remove(dumpPath);

	CHECK(lines > Engine::Log::RING_CAPACITY / 2);
	CHECK(contiguous);
}

static void testLogThreadSafety()
{
	std::printf("EngineLog from 8 threads\n");

	Engine::Log::setMinLevel(Engine::LOG_TRACE);

	// Write to a file and then check that every line is complete and well-formed -- lines from
	// different threads must never interleave.
	const char *path = "enginetests-log.txt";
	std::remove(path);
	CHECK(Engine::Log::openFile(path));

	const int threadCount = 8;
	const int linesPerThread = 500;
	std::vector<std::thread> threads;
	for (int t = 0; t < threadCount; ++t)
	{
		threads.push_back(std::thread([t, linesPerThread]() {
			Engine::setThreadRole(Engine::THREAD_ROLE_WORKER, nullptr);
			for (int i = 0; i < linesPerThread; ++i)
				Engine::Log::write(Engine::LOG_CAT_JOB, Engine::LOG_INFO, "T%02d-%04d-ENDE", t, i);
		}));
	}
	for (std::size_t i = 0; i < threads.size(); ++i)
		threads[i].join();
	Engine::Log::closeFile();

	std::FILE *f = std::fopen(path, "r");
	CHECK(f != nullptr);
	int wellFormed = 0, malformed = 0;
	if (f != nullptr)
	{
		char line[512];
		while (std::fgets(line, sizeof(line), f) != nullptr)
		{
			const char *marker = std::strstr(line, "T");
			const size_t len = std::strlen(line);
			// Every line we wrote ends in "-ENDE\n" and contains exactly one "ENDE"
			const char *firstEnd = std::strstr(line, "ENDE");
			if (firstEnd != nullptr)
			{
				// complete: the line ends exactly in "ENDE\n" and contains the marker only once
				const bool endsWell = (len >= 5) && (std::strcmp(line + len - 5, "ENDE\n") == 0);
				const bool onlyOnce = (std::strstr(firstEnd + 4, "ENDE") == nullptr);
				if (endsWell && onlyOnce && marker != nullptr)
					++wellFormed;
				else
					++malformed;
			}
		}
		std::fclose(f);
	}
	std::printf("  %d lines well-formed, %d interleaved\n", wellFormed, malformed);
	CHECK(malformed == 0);
	CHECK(wellFormed == threadCount * linesPerThread);
	std::remove(path);

	Engine::setThreadRole(Engine::THREAD_ROLE_MAIN_AND_SIM, nullptr);
}

static void testLogWorkerAsserts()
{
	std::printf("EngineLog assert from worker threads\n");

	// Drain existing entries
	char buffer[512];
	while (Engine::Log::pollPendingAssert(buffer, sizeof(buffer)))
		;

	std::thread worker([]() {
		Engine::setThreadRole(Engine::THREAD_ROLE_WORKER, nullptr);
		Engine::Log::postAssertFromThread("Job.cpp", 42, "runJob", "test assertion violated");
	});
	worker.join();

	CHECK(Engine::Log::pendingAssertCount() == 1);
	CHECK(Engine::Log::pollPendingAssert(buffer, sizeof(buffer)));
	CHECK(std::strstr(buffer, "Job.cpp") != nullptr);
	CHECK(std::strstr(buffer, "test assertion violated") != nullptr);
	CHECK(!Engine::Log::pollPendingAssert(buffer, sizeof(buffer)));
}

// ------------------------------------------------------------- CPU topology
static void testCpuTopology()
{
	std::printf("CpuTopology\n");

	const Engine::CpuTopology &topo = Engine::CpuTopology::get();
	char text[256];
	topo.describe(text, sizeof(text));
	std::printf("  %s\n", text);

	CHECK(topo.logicalProcessors >= 1);
	CHECK(topo.physicalCores >= 1);
	CHECK(topo.logicalProcessors >= topo.physicalCores);
	CHECK(topo.performanceCores + topo.efficiencyCores == topo.physicalCores);
	CHECK(topo.recommendedWorkerCount() >= 1);
	// There is always one NUMA node; a counting bug would show as 2 on a single-node system.
	CHECK(topo.numaNodes >= 1);
	CHECK(topo.numaNodes <= topo.logicalProcessors);
	CHECK(topo.recommendedWorkerCount() <= (topo.logicalProcessors > 2 ? topo.logicalProcessors - 2 : 1));
	CHECK(topo.hasSimultaneousMultithreading == (topo.logicalProcessors > topo.physicalCores)
		|| !topo.hasSimultaneousMultithreading);
	// Cached: the same record on every call
	CHECK(&topo == &Engine::CpuTopology::get());
}

// ------------------------------------------------- String reference counting
//
// AsciiString/UnicodeString free their buffer when the reference count reaches 0. Decrementing
// and THEN checking for 0 separately is racy: if two threads release the same string, both can
// see 0 and the buffer is freed twice. The correct protocol ties the free to the return value of
// the atomic decrement.
//
// The strings themselves need the engine memory pool and cannot be built here, so this test
// checks the protocol on a model with both variants. A real stress test with AsciiString
// belongs in a Windows run.
namespace
{
	/// Model of a shared string buffer: refCount as in AsciiStringData, plus a liveness marker
	/// that proves a double free unambiguously.
	struct RefCounted
	{
		std::atomic<long>	refCount;		// reference count
		std::atomic<int>	live;			// 1 = buffer exists, 0 = freed
		std::atomic<int>	frees;			// frees in this round
		std::atomic<int>	doubleFrees;	// frees of an already freed buffer
	};

	void freeBuffer(RefCounted &buffer)
	{
		int expected = 1;
		if (buffer.live.compare_exchange_strong(expected, 0))
			++buffer.frees;
		else
			++buffer.doubleFrees;	// the buffer was already gone -- the double-free bug
	}

	/// Wrong (original): decrement, then check for 0 separately.
	void releaseOldWay(RefCounted &buffer)
	{
		buffer.refCount.fetch_sub(1);
		if (buffer.refCount.load() == 0)
			freeBuffer(buffer);
	}

	/// Correct: only the thread whose decrement yields 0 frees.
	void releaseNewWay(RefCounted &buffer)
	{
		if (buffer.refCount.fetch_sub(1) == 1)
			freeBuffer(buffer);
	}

	/// N threads each hold a reference to the same buffer and release it at the same time.
	/// Exactly one may free. The buffer is rebuilt each round; counts accumulate over all rounds.
	struct HammerResult
	{
		int freesTotal = 0;
		int doubleFreesTotal = 0;
		int roundsWithoutFree = 0;
	};

	HammerResult hammerSharedRelease(void (*release)(RefCounted &), int threadCount, int rounds)
	{
		RefCounted buffer;
		HammerResult result;
		buffer.refCount.store(threadCount);
		buffer.live.store(1);
		buffer.frees.store(0);
		buffer.doubleFrees.store(0);

		std::barrier sync(threadCount, [&]() noexcept {
			// Runs once all threads have released their reference: exactly one free per round.
			result.freesTotal += buffer.frees.load();
			result.doubleFreesTotal += buffer.doubleFrees.load();
			if (buffer.frees.load() == 0)
				++result.roundsWithoutFree;
			buffer.refCount.store(threadCount);
			buffer.live.store(1);
			buffer.frees.store(0);
			buffer.doubleFrees.store(0);
		});

		std::vector<std::thread> threads;
		for (int t = 0; t < threadCount; ++t)
		{
			threads.push_back(std::thread([&buffer, &sync, release, rounds]() {
				for (int r = 0; r < rounds; ++r)
				{
					release(buffer);
					sync.arrive_and_wait();
				}
			}));
		}
		for (std::size_t i = 0; i < threads.size(); ++i)
			threads[i].join();
		return result;
	}
}

static void testStringRefCountProtocol()
{
	std::printf("Reference count protocol\n");

	const int threadCount = 8;
	const int rounds = 8000;

	// Correct variant: exactly one free per round, none doubled, none missed.
	const HammerResult fixed = hammerSharedRelease(releaseNewWay, threadCount, rounds);
	std::printf("  new:  %d releases in %d rounds, %d double, %d rounds without release\n",
		fixed.freesTotal, rounds, fixed.doubleFreesTotal, fixed.roundsWithoutFree);
	CHECK(fixed.doubleFreesTotal == 0);
	CHECK(fixed.roundsWithoutFree == 0);
	CHECK(fixed.freesTotal == rounds);

	// Racy variant for illustration only: the bug depends on interleaving and need not occur in
	// every run, so the test does not require it.
	const HammerResult old = hammerSharedRelease(releaseOldWay, threadCount, rounds);
	std::printf("  old:  %d releases in %d rounds, %d double (the original double release)\n",
		old.freesTotal, rounds, old.doubleFreesTotal);
}

// ------------------------------------------------- Drawable lookup table (GameClient)
//
// GameClient keeps drawables in a vector indexed by DrawableID. IDs increase monotonically over
// the whole session, and the vector grows by doubling. reset() must destroy the drawables
// **before** shrinking the vector back to its initial size; otherwise each destruction writes
// m_drawableVector[id] with id far beyond size().
//
// The real GameClient needs half the engine, so this is a model with the same sequence.
namespace
{
	struct DrawableTableModel
	{
		static const std::size_t INITIAL_SIZE = 8192;

		std::vector<void *>	table;
		unsigned			nextId = 1;
		unsigned			outOfRangeWrites = 0;
		bool				clearTableFirst = false;	///< true = wrong order in reset()

		unsigned add()
		{
			const unsigned id = nextId++;
			while (id >= table.size())
				table.resize(table.empty() ? INITIAL_SIZE : table.size() * 2, nullptr);
			table[id] = reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));
			return id;
		}

		void remove(unsigned id)
		{
			if (id < table.size())
				table[id] = nullptr;
			else
				++outOfRangeWrites;	// the real code would write out of bounds here
		}

		void reset(const std::vector<unsigned> &live)
		{
			if (clearTableFirst)
			{
				table.clear();
				table.resize(INITIAL_SIZE, nullptr);
			}
			for (std::size_t i = 0; i < live.size(); ++i)
				remove(live[i]);
			if (!clearTableFirst)
			{
				table.clear();
				table.resize(INITIAL_SIZE, nullptr);
			}
		}
	};

	unsigned runDrawableTableRounds(bool clearTableFirst)
	{
		DrawableTableModel model;
		model.clearTableFirst = clearTableFirst;
		// three games with 5000 drawables each: from the second one on, IDs are above 8192
		for (int round = 0; round < 3; ++round)
		{
			std::vector<unsigned> live;
			for (int i = 0; i < 5000; ++i)
				live.push_back(model.add());
			model.reset(live);
		}
		return model.outOfRangeWrites;
	}

	/// The same table in GameLogic (m_objVector). Difference: IDs restart at 1 per game, so the
	/// bug only hits games with more than 8192 objects, but then just the same.
	unsigned runObjectTableRounds(bool clearTableFirst)
	{
		DrawableTableModel model;
		model.clearTableFirst = clearTableFirst;
		for (int round = 0; round < 3; ++round)
		{
			std::vector<unsigned> live;
			for (int i = 0; i < 12000; ++i)
				live.push_back(model.add());
			model.reset(live);
			model.nextId = 1;	// GameLogic::reset() resets m_nextObjID
		}
		return model.outOfRangeWrites;
	}
}

static void testDrawableLookupTable()
{
	std::printf("GameClient drawable lookup table (model)\n");

	// wrong order: shrink the table first -> out-of-range writes
	const unsigned oldOrder = runDrawableTableRounds(true);
	std::printf("  old order: %u accesses outside the vector\n", oldOrder);
	CHECK(oldOrder > 0);

	// correct order: destroy first, then clear the table
	const unsigned newOrder = runDrawableTableRounds(false);
	CHECK(newOrder == 0);

	// GameLogic::m_objVector, same mechanism
	const unsigned oldOrderObjects = runObjectTableRounds(true);
	std::printf("  GameLogic, old order: %u accesses outside the vector\n", oldOrderObjects);
	CHECK(oldOrderObjects > 0);
	CHECK(runObjectTableRounds(false) == 0);
}

// -------------------------------------------------- Generations (object handles)
//
// The mechanism behind ObjectHandle: each slot in the object table has a counter that increases
// on occupy and free. A stored generation (= handle) no longer matches afterwards.
static void testGenerationTable()
{
	std::printf("GenerationTable\n");

	Engine::GenerationTable table;
	table.reset(4);
	CHECK(table.capacity() == 4);

	// Never occupied slot: generation 0, and 0 matches nothing (an empty handle never resolves).
	CHECK(table.generation(2) == 0);
	CHECK(!table.matches(2, 0));

	// Object A gets slot 2
	const Engine::Generation genA = table.onSlotOccupied(2);
	CHECK(genA == 1);
	CHECK(table.matches(2, genA));

	// A dies: the handle to A no longer matches
	table.onSlotFreed(2);
	CHECK(!table.matches(2, genA));

	// Object B gets the same slot -- exactly the case the handle protects against
	const Engine::Generation genB = table.onSlotOccupied(2);
	CHECK(genB != genA);
	CHECK(table.matches(2, genB));
	CHECK(!table.matches(2, genA));

	// Other slots stay untouched
	CHECK(table.generation(0) == 0);
	CHECK(table.generation(3) == 0);

	// The table grows; beyond capacity there are no matches, but no crash either
	CHECK(table.generation(9999) == 0);
	CHECK(!table.matches(9999, 1));
	const Engine::Generation genFar = table.onSlotOccupied(100);
	CHECK(table.capacity() >= 101);
	CHECK(table.matches(100, genFar));

	// reset() invalidates all handles (new game)
	table.reset(8);
	CHECK(!table.matches(2, genB));
	CHECK(!table.matches(100, genFar));
	CHECK(table.capacity() == 8);
}

// ------------------------------------------------------- Job system
//
// Checks: correctness, clean shutdown, no deadlock with nested jobs, and bit-identical results
// with 0, 1, 7 and 31 workers.
static void testJobSystemBasics()
{
	std::printf("Job system: correctness\n");

	Engine::JobSystem &jobs = Engine::JobSystem::get();

	// Serial reference mode
	jobs.start(0);
	CHECK(jobs.workerCount() == 0);
	{
		std::vector<int> values(1000, 0);
		Engine::parallelFor(values.size(), 64, [&](std::size_t begin, std::size_t end) {
			for (std::size_t i = begin; i < end; ++i)
				values[i] = static_cast<int>(i) * 2;
		});
		bool allOk = true;
		for (std::size_t i = 0; i < values.size(); ++i)
			if (values[i] != static_cast<int>(i) * 2)
				allOk = false;
		CHECK(allOk);
	}

	// With workers: each element exactly once, no gap, no duplicate work
	jobs.start(4);
	CHECK(jobs.workerCount() == 4);
	{
		const std::size_t count = 10000;
		std::vector<std::atomic<int> > touched(count);
		for (std::size_t i = 0; i < count; ++i)
			touched[i].store(0);
		Engine::parallelFor(count, 64, [&](std::size_t begin, std::size_t end) {
			for (std::size_t i = begin; i < end; ++i)
				touched[i].fetch_add(1);
		});
		int wrong = 0;
		for (std::size_t i = 0; i < count; ++i)
			if (touched[i].load() != 1)
				++wrong;
		CHECK(wrong == 0);
	}

	// Edge cases: count 0, grain 0, count < grain
	{
		int calls = 0;
		Engine::parallelFor(0, 8, [&](std::size_t, std::size_t) { ++calls; });
		CHECK(calls == 0);

		std::atomic<int> sum(0);
		Engine::parallelFor(5, 0, [&](std::size_t begin, std::size_t end) {
			for (std::size_t i = begin; i < end; ++i)
				sum.fetch_add(1);
		});
		CHECK(sum.load() == 5);

		std::atomic<int> blocks(0);
		Engine::parallelFor(3, 1000, [&](std::size_t begin, std::size_t end) {
			blocks.fetch_add(1);
			CHECK(begin == 0 && end == 3);
		});
		CHECK(blocks.load() == 1);
	}

	// Waiting threads help. Deterministic proof: all workers are busy with long background jobs;
	// the frame work must still complete immediately, done by the caller itself.
	{
		jobs.start(2);
		std::atomic<int> busyDone(0);
		for (int i = 0; i < 2; ++i)
		{
			jobs.submitBackground([&busyDone]() {
				std::this_thread::sleep_for(std::chrono::milliseconds(400));
				busyDone.fetch_add(1);
			});
		}
		// wait briefly so the workers have really picked up the long jobs
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		const unsigned long long before = jobs.blocksExecutedByCallers();
		const Engine::ScopedTimer timer;
		std::atomic<int> sum(0);
		Engine::parallelFor(4000, 8, [&](std::size_t begin, std::size_t end) {
			for (std::size_t i = begin; i < end; ++i)
				sum.fetch_add(1);
		});
		const double elapsedMs = timer.elapsedMilliseconds();
		CHECK(sum.load() == 4000);
		// All workers are stuck in the 400 ms jobs -- only the caller can have done the blocks.
		CHECK(jobs.blocksExecutedByCallers() > before);
		CHECK(elapsedMs < 350.0);
		std::printf("  frame work with busy workers: %.1f ms, %llu blocks by the caller\n",
			elapsedMs, static_cast<unsigned long long>(jobs.blocksExecutedByCallers() - before));

		for (int spin = 0; spin < 2000 && busyDone.load() < 2; ++spin)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		CHECK(busyDone.load() == 2);
		jobs.start(4);
	}

	// An exception in a block must not hang the caller: it is caught and counted, and the block
	// counts as done. (Otherwise parallelFor would wait forever on the pending counter -- in the
	// game a hang instead of an error message.)
	{
		const unsigned long long exceptionsBefore = jobs.jobExceptionCount();
		std::atomic<int> done(0);
		Engine::parallelFor(400, 10, [&](std::size_t begin, std::size_t end) {
			if (begin == 100)
				throw std::runtime_error("test exception from a job");
			done.fetch_add(static_cast<int>(end - begin));
		});
		CHECK(done.load() == 390);		// all blocks except the throwing one
		CHECK(jobs.jobExceptionCount() == exceptionsBefore + 1);
	}

	// Nested jobs: the outer block waits for inner blocks. Waiting threads help, so this must not
	// deadlock.
	{
		std::atomic<int> inner(0);
		Engine::parallelFor(16, 1, [&](std::size_t, std::size_t) {
			Engine::parallelFor(100, 10, [&](std::size_t b, std::size_t e) {
				inner.fetch_add(static_cast<int>(e - b));
			});
		});
		CHECK(inner.load() == 16 * 100);
	}

	// Background jobs run and do not disturb frame jobs
	{
		std::atomic<int> done(0);
		for (int i = 0; i < 20; ++i)
			jobs.submitBackground([&done]() { done.fetch_add(1); });
		// Frame work in between; background jobs get done along the way
		std::atomic<int> frameWork(0);
		Engine::parallelFor(2000, 32, [&](std::size_t b, std::size_t e) { frameWork.fetch_add(static_cast<int>(e - b)); });
		CHECK(frameWork.load() == 2000);
		// Wait until the remaining background jobs are done too
		for (int spin = 0; spin < 1000 && done.load() < 20; ++spin)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		CHECK(done.load() == 20);
	}

	// Shutdown: running work finishes, afterwards everything runs serially again
	jobs.shutdown();
	CHECK(jobs.workerCount() == 0);
	{
		std::atomic<int> sum(0);
		Engine::parallelFor(100, 7, [&](std::size_t b, std::size_t e) { sum.fetch_add(static_cast<int>(e - b)); });
		CHECK(sum.load() == 100);
	}
}

/// Computes an order-sensitive value: block results combined in block order (like the CRC
/// combination).
static unsigned long long orderedChecksum(std::size_t count, std::size_t grain)
{
	return Engine::parallelMapOrdered<unsigned long long>(count, grain, 0ull,
		[](std::size_t begin, std::size_t end) -> unsigned long long {
			unsigned long long h = 0;
			for (std::size_t i = begin; i < end; ++i)
				h = h * 1315423911ull + static_cast<unsigned long long>(i * 2654435761ull);
			return h;
		},
		[](unsigned long long acc, unsigned long long block) -> unsigned long long {
			return acc * 1099511628211ull + block;	// order matters!
		});
}

static void testParallelDeterminism()
{
	std::printf("Job system: determinism with 0/1/7/31 workers\n");

	Engine::JobSystem &jobs = Engine::JobSystem::get();
	const int workerCounts[] = { 0, 1, 7, 31 };
	unsigned long long reference = 0;
	std::vector<float> referenceValues;

	for (int w = 0; w < 4; ++w)
	{
		jobs.start(workerCounts[w]);

		// 1) Ordered reduction
		const unsigned long long checksum = orderedChecksum(5000, 64);

		// 2) Element-wise results (floating point, so rounding is compared too)
		std::vector<float> values(5000, 0.0f);
		Engine::parallelFor(values.size(), 64, [&](std::size_t begin, std::size_t end) {
			for (std::size_t i = begin; i < end; ++i)
				values[i] = static_cast<float>(i) * 0.1f + static_cast<float>(i % 7) * 0.01f;
		});

		if (w == 0)
		{
			reference = checksum;
			referenceValues = values;
		}
		else
		{
			CHECK(checksum == reference);
			CHECK(values.size() == referenceValues.size());
			bool bitwiseEqual = true;
			for (std::size_t i = 0; i < values.size(); ++i)
				if (std::memcmp(&values[i], &referenceValues[i], sizeof(float)) != 0)
					bitwiseEqual = false;
			CHECK(bitwiseEqual);
		}
		std::printf("  %2d workers: checksum %016llX\n", workerCounts[w], checksum);
	}
	jobs.shutdown();
}

// ------------------------------------------------------------- Random streams
static void testSimRandom()
{
	std::printf("SimRandom\n");

	// Same input, same result -- always, everywhere
	const std::uint32_t seed = 12345, frame = 678;
	const std::uint32_t a = Engine::SimRandom::value(seed, frame, 42, Engine::SIM_SALT_TARGET_SCAN);
	CHECK(a == Engine::SimRandom::value(seed, frame, 42, Engine::SIM_SALT_TARGET_SCAN));

	// Every input changes the result
	CHECK(a != Engine::SimRandom::value(seed + 1, frame, 42, Engine::SIM_SALT_TARGET_SCAN));
	CHECK(a != Engine::SimRandom::value(seed, frame + 1, 42, Engine::SIM_SALT_TARGET_SCAN));
	CHECK(a != Engine::SimRandom::value(seed, frame, 43, Engine::SIM_SALT_TARGET_SCAN));
	CHECK(a != Engine::SimRandom::value(seed, frame, 42, Engine::SIM_SALT_PATHFIND));

	// Range is respected and the distribution is not degenerate
	int histogram[6] = { 0, 0, 0, 0, 0, 0 };
	for (std::uint32_t key = 0; key < 60000; ++key)
	{
		const int v = Engine::SimRandom::valueInRange(1, 6, seed, frame, key, Engine::SIM_SALT_GENERIC);
		CHECK_SILENT(v >= 1 && v <= 6);
		++histogram[v - 1];
	}
	bool balanced = true;
	for (int i = 0; i < 6; ++i)
		if (histogram[i] < 8000 || histogram[i] > 12000)
			balanced = false;
	CHECK(balanced);
	std::printf("  dice over 60000 keys: %d %d %d %d %d %d\n",
		histogram[0], histogram[1], histogram[2], histogram[3], histogram[4], histogram[5]);

	// Wide range: hi - lo overflows int, the computation must use 64 bits
	{
		bool wideRangeOk = true;
		for (std::uint32_t key = 0; key < 20000; ++key)
		{
			const int v = Engine::SimRandom::valueInRange(-2000000000, 2000000000, seed, frame, key,
				Engine::SIM_SALT_GENERIC);
			if (v < -2000000000 || v > 2000000000)
				wideRangeOk = false;
		}
		CHECK(wideRangeOk);
		// Edge cases: full int range and a single value
		const int full = Engine::SimRandom::valueInRange(-2147483647 - 1, 2147483647, seed, frame, 7,
			Engine::SIM_SALT_GENERIC);
		CHECK(full >= -2147483647 - 1 && full <= 2147483647);
		CHECK(Engine::SimRandom::valueInRange(5, 5, seed, frame, 7, Engine::SIM_SALT_GENERIC) == 5);
	}

	// [0,1) float
	bool inUnitRange = true;
	for (std::uint32_t key = 0; key < 10000; ++key)
	{
		const float f = Engine::SimRandom::valueUnitFloat(seed, frame, key, Engine::SIM_SALT_VISION);
		if (!(f >= 0.0f && f < 1.0f))
			inUnitRange = false;
	}
	CHECK(inUnitRange);

	// Stream at one site: different indices, different values
	CHECK(Engine::SimRandom::valueAt(seed, frame, 7, 0, 0) != Engine::SimRandom::valueAt(seed, frame, 7, 0, 1));

	// Independent of thread and call order: 8 threads compute the same table in different orders
	// and must get the same result.
	const std::size_t count = 20000;
	std::vector<std::uint32_t> serial(count);
	for (std::size_t i = 0; i < count; ++i)
		serial[i] = Engine::SimRandom::value(seed, frame, static_cast<std::uint32_t>(i), Engine::SIM_SALT_PATHFIND);

	std::vector<std::uint32_t> parallel(count, 0);
	Engine::JobSystem::get().start(8);
	Engine::parallelFor(count, 32, [&](std::size_t begin, std::size_t end) {
		for (std::size_t i = end; i > begin; --i)	// deliberately backwards
			parallel[i - 1] = Engine::SimRandom::value(seed, frame, static_cast<std::uint32_t>(i - 1), Engine::SIM_SALT_PATHFIND);
	});
	Engine::JobSystem::get().shutdown();
	CHECK(serial == parallel);
}

// ------------------------------------------------------- Logic access guard
static void testLogicAccessGuard()
{
	std::printf("LogicAccessGuard\n");

	// Initially the logic is closed (no tick running)
	CHECK(!Engine::isLogicAccessOpen());

	{
		Engine::ScopedLogicAccess tick;			// tick starts
		CHECK(Engine::isLogicAccessOpen());
		{
			Engine::ScopedLogicAccess nested;	// nested (loading screen, camera move)
			CHECK(Engine::isLogicAccessOpen());
		}
		// the inner section must not close early
		CHECK(Engine::isLogicAccessOpen());
	}
	CHECK(!Engine::isLogicAccessOpen());

	// The counter is per thread: when the sim thread opens its tick, the client thread must not
	// count as "in the tick" too (otherwise the guard would swallow exactly the findings it is for).
	{
		Engine::ScopedLogicAccess tickOnThisThread;
		bool openInOtherThread = true;
		std::thread other([&]() { openInOtherThread = Engine::isLogicAccessOpen(); });
		other.join();
		CHECK(!openInOtherThread);
		CHECK(Engine::isLogicAccessOpen());
	}

	// A site is reported exactly once, but every access is counted
	const std::size_t findingsBefore = Engine::logicAccessFindingCount();
	const unsigned long long totalBefore = Engine::logicAccessTotalCount();
	CHECK(Engine::reportLogicAccess("Drawable.cpp", 1234, "Drawable", "drawHealthBar"));
	CHECK(!Engine::reportLogicAccess("Drawable.cpp", 1234, "Drawable", "drawHealthBar"));
	CHECK(!Engine::reportLogicAccess("Drawable.cpp", 1234, "Drawable", "drawHealthBar"));
	CHECK(Engine::reportLogicAccess("Drawable.cpp", 1299, "Drawable", "drawAmmo"));
	CHECK(Engine::logicAccessFindingCount() == findingsBefore + 2);
	CHECK(Engine::logicAccessTotalCount() == totalBefore + 4);

	// Write the findings list
	const char *path = "enginetests-e4a.txt";
	std::remove(path);
	CHECK(Engine::writeLogicAccessFindingsTo(path));
	std::FILE *f = std::fopen(path, "r");
	CHECK(f != nullptr);
	bool sawHealthBar = false, sawAmmo = false;
	if (f != nullptr)
	{
		char line[512];
		while (std::fgets(line, sizeof(line), f) != nullptr)
		{
			if (std::strstr(line, "drawHealthBar") != nullptr)
				sawHealthBar = true;
			if (std::strstr(line, "drawAmmo") != nullptr)
				sawAmmo = true;
		}
		std::fclose(f);
	}
	CHECK(sawHealthBar);
	CHECK(sawAmmo);
	std::remove(path);
}

// ------------------------------------------------------------------ SimThread
static std::vector<unsigned> s_renderOwners;
static void recordRenderOwner(unsigned threadId) { s_renderOwners.push_back(threadId); }

static void testSimThread()
{
	std::printf("SimThread\n");

	Engine::SimThread sim;
	CHECK(!sim.isRunning());
	CHECK(sim.start());
	CHECK(!sim.start());		// already running
	CHECK(sim.isRunning());
	CHECK(!sim.isCurrentThread());

	// the call runs on the other thread, with the Sim role, and the caller waits for it
	std::thread::id ranOn;
	bool roleSim = false, roleMain = true, current = false;
	int value = 0;
	sim.runAndWait([&]() {
		ranOn = std::this_thread::get_id();
		roleSim = Engine::threadHasRole(Engine::THREAD_ROLE_SIM);
		roleMain = Engine::threadHasRole(Engine::THREAD_ROLE_MAIN);
		current = sim.isCurrentThread();
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		value = 42;
	});
	CHECK(value == 42);			// visible right after the call: the caller waited
	CHECK(ranOn != std::this_thread::get_id());
	CHECK(roleSim);
	CHECK(!roleMain);
	CHECK(current);

	// the render-owner hook hands over to the simulation thread and back to the caller
	s_renderOwners.clear();
	Engine::SimThread::setRenderOwnerHook(recordRenderOwner);
	sim.runAndWait([]() {});
	Engine::SimThread::setRenderOwnerHook(nullptr);
	CHECK(s_renderOwners.size() == 2);
	if (s_renderOwners.size() == 2)
		CHECK(s_renderOwners[0] != s_renderOwners[1]);

	// an exception reaches the caller with its type, and the thread keeps working
	bool caught = false;
	try
	{
		sim.runAndWait([]() { throw 7; });
	}
	catch (int code)
	{
		caught = (code == 7);
	}
	CHECK(caught);
	int calls = 0;
	for (int i = 0; i < 1000; ++i)
		sim.runAndWait([&]() { ++calls; });
	CHECK(calls == 1000);
	CHECK(sim.runCount() == 1003);

	sim.stop();
	CHECK(!sim.isRunning());
	sim.stop();					// harmless twice
	CHECK(sim.start());			// and can start again
	sim.runAndWait([&]() { ++calls; });
	CHECK(calls == 1001);
	sim.stop();
}

// ------------------------------------------------------------------ Watchdog
static void testWatchdog()
{
	std::printf("Watchdog\n");

	Engine::Watchdog &dog = Engine::Watchdog::get();

	// Short timeout so the test stays fast: 150 ms instead of 10 s.
	std::atomic<int> stalls(0);			// sim thread only -- the main thread keeps reporting in the test
	std::atomic<int> reportedSubject(-1);
	std::atomic<unsigned> reportedStall(0);
	dog.setCallback([&](Engine::WatchdogSubject subject, unsigned stalledMs) {
		if (subject != Engine::WATCHDOG_SIM)
			return;
		reportedSubject.store(static_cast<int>(subject));
		reportedStall.store(stalledMs);
		stalls.fetch_add(1);
	});
	dog.start(150, 20);
	CHECK(dog.isRunning());

	// Regular heartbeats: no hang
	for (int i = 0; i < 20; ++i)
	{
		dog.heartbeat(Engine::WATCHDOG_MAIN);
		dog.heartbeat(Engine::WATCHDOG_SIM);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	CHECK(stalls.load() == 0);

	// Artificial hang in the sim thread: the main thread keeps reporting, the sim does not
	const unsigned stallsBefore = dog.stallCount();
	for (int i = 0; i < 40 && stalls.load() == 0; ++i)
	{
		dog.heartbeat(Engine::WATCHDOG_MAIN);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	CHECK(stalls.load() >= 1);
	CHECK(reportedSubject.load() == (int)Engine::WATCHDOG_SIM);
	CHECK(reportedStall.load() >= 150);
	CHECK(dog.stallCount() > stallsBefore);
	std::printf("  hang detected: %s after %u ms\n",
		Engine::Watchdog::subjectName(static_cast<Engine::WatchdogSubject>(reportedSubject.load())), reportedStall.load());

	// A hang is reported only once, not on every poll (the main thread keeps reporting so only
	// the sim hang is in play)
	const int afterFirst = stalls.load();
	for (int i = 0; i < 20; ++i)
	{
		dog.heartbeat(Engine::WATCHDOG_MAIN);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	CHECK(stalls.load() == afterFirst);

	// After a heartbeat monitoring is armed again
	dog.heartbeat(Engine::WATCHDOG_SIM);
	CHECK(dog.millisecondsSinceHeartbeat(Engine::WATCHDOG_SIM) < 100);

	// Suspended subjects report nothing (loading screen, pause, debugger)
	dog.suspend(Engine::WATCHDOG_SIM);
	dog.suspend(Engine::WATCHDOG_MAIN);
	const int beforeSuspend = stalls.load();
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	CHECK(stalls.load() == beforeSuspend);
	dog.resume(Engine::WATCHDOG_SIM);
	dog.resume(Engine::WATCHDOG_MAIN);

	// Suspensions nest (loading a save game starts a new game inside): the inner resume must not arm the
	// subject again, only the outer one does.
	{
		Engine::WatchdogSuspension outer;
		{
			Engine::WatchdogSuspension inner;
		}
		const int beforeNested = stalls.load();
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		CHECK(stalls.load() == beforeNested);
	}
	CHECK(dog.millisecondsSinceHeartbeat(Engine::WATCHDOG_SIM) < 100);	// armed again with a fresh clock
	{
		const int beforeArmed = stalls.load();
		for (int i = 0; i < 40 && stalls.load() == beforeArmed; ++i)
		{
			dog.heartbeat(Engine::WATCHDOG_MAIN);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		CHECK(stalls.load() > beforeArmed);	// monitored again: the silent sim thread is reported
	}
	dog.resume(Engine::WATCHDOG_SIM);	// a resume without suspend only resets the clock
	CHECK(dog.millisecondsSinceHeartbeat(Engine::WATCHDOG_SIM) < 100);

	dog.stop();
	CHECK(!dog.isRunning());
	dog.setCallback(Engine::WatchdogCallback());
}

// ------------------------------------------------- Verification mode for parallel sections
//
// `-verifyParallel` additionally computes each section serially and compares. This guards
// parallel logic: a mismatch shows up immediately with the section name instead of as a desync
// minutes later.
static void testVerifyParallel()
{
	std::printf("Verify mode for parallel sections\n");

	Engine::JobSystem &jobs = Engine::JobSystem::get();
	jobs.start(4);

	// 1) Clean computation: verification must report nothing and not change the result.
	jobs.setVerifyParallel(false);
	const unsigned long long withoutVerify = orderedChecksum(2000, 64);

	const unsigned long long mismatchesBefore = jobs.verifyMismatchCount();
	jobs.setVerifyParallel(true);
	const unsigned long long withVerify = orderedChecksum(2000, 64);
	CHECK(withVerify == withoutVerify);
	CHECK(jobs.verifyMismatchCount() == mismatchesBefore);

	// 2) A deliberately unstable computation: the second (serial) pass yields something else
	//    than the first. Verification must report it and take the serial result.
	std::atomic<int> callCount(0);
	const unsigned long long result = Engine::parallelMapOrdered<unsigned long long>(
		256, 64, 0ull,
		[&](std::size_t begin, std::size_t) -> unsigned long long {
			// The second pass over the same block yields a different value.
			const int n = callCount.fetch_add(1);
			return static_cast<unsigned long long>(begin + (n >= 4 ? 1000 : 0));
		},
		[](unsigned long long acc, unsigned long long block) -> unsigned long long {
			return acc + block;
		},
		Engine::JOB_PRIORITY_FRAME, "Testabschnitt");
	CHECK(jobs.verifyMismatchCount() > mismatchesBefore);
	// 4 blocks, each 1000 larger in the second pass: the serial result wins.
	CHECK(result == (0 + 64 + 128 + 192) + 4 * 1000);
	std::printf("  mismatches detected: %llu\n", jobs.verifyMismatchCount() - mismatchesBefore);

	// An absurd worker count from -workers/Options.ini is capped instead of used
	jobs.shutdown();
	jobs.start(100000);
	CHECK(jobs.workerCount() == Engine::JobSystem::MAX_WORKER_COUNT);
	jobs.shutdown();
	jobs.start(4);

	// 3) The counter must survive the pool: -verifyParallel runs in long tests, and
	//    start()/shutdown() recreate the pool. A counter inside the pool would lose the finding
	//    exactly when it is needed.
	const unsigned long long beforeRestart = jobs.verifyMismatchCount();
	CHECK(beforeRestart > 0);
	jobs.shutdown();
	CHECK(jobs.verifyMismatchCount() == beforeRestart);
	jobs.start(2);
	CHECK(jobs.verifyMismatchCount() == beforeRestart);

	jobs.setVerifyParallel(false);
	jobs.shutdown();
}

// ------------------------------------------------------------------ Async tasks
//
// AsyncTask with a fixed take-over tick: started at tick N, waited for at tick N + k. The result
// must be the same with any worker count, a thread waiting in parallelFor() must not pick up an
// async job, and shutdown() must run queued jobs so no waiter hangs.
static unsigned long long asyncLatencyRun(int workers)
{
	Engine::JobSystem &jobs = Engine::JobSystem::get();
	jobs.shutdown();
	jobs.start(workers);

	const int LATENCY = 3;
	unsigned long long state = 1469598103934665603ull;
	std::vector<unsigned long long> input;
	unsigned long long output = 0;
	int startedAt = -1;
	Engine::AsyncTask task;
	for (int tick = 0; tick < 200; ++tick)
	{
		if (startedAt >= 0 && tick == startedAt + LATENCY)
		{
			task.wait();
			state = (state ^ output) * 1099511628211ull;
			startedAt = -1;
		}
		if (startedAt < 0 && tick % 5 == 0)
		{
			// The job sees only its copied input; the state changes meanwhile.
			input.assign(64, 0);
			for (std::size_t i = 0; i < input.size(); ++i)
				input[i] = state + i * static_cast<unsigned long long>(tick);
			task.start([&input, &output]() {
				unsigned long long h = 0;
				for (std::size_t i = 0; i < input.size(); ++i)
					h = h * 31 + input[i];
				output = h;
			});
			startedAt = tick;
		}
		state = state * 6364136223846793005ull + 1442695040888963407ull;
	}
	task.wait();
	jobs.shutdown();
	return state;
}

static void testAsyncTask()
{
	std::printf("Async tasks with fixed latency\n");
	Engine::JobSystem &jobs = Engine::JobSystem::get();

	// Without a pool the job runs inside start().
	jobs.shutdown();
	{
		int value = 0;
		Engine::AsyncTask task;
		task.start([&value]() { value = 7; });
		CHECK(value == 7);
		CHECK(!task.isRunning());
	}

	// With a pool: wait() returns once the job is done.
	jobs.start(4);
	{
		std::atomic<int> value(0);
		Engine::AsyncTask task;
		task.start([&value]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			value.store(9);
		});
		task.wait();
		CHECK(value.load() == 9);
		CHECK(!task.isRunning());
	}

	// Same result with 0, 1, 4 and 8 workers.
	const unsigned long long reference = asyncLatencyRun(0);
	CHECK(asyncLatencyRun(1) == reference);
	CHECK(asyncLatencyRun(4) == reference);
	CHECK(asyncLatencyRun(8) == reference);

	// A thread waiting in parallelFor() helps with frame jobs only.
	jobs.shutdown();
	jobs.start(1);
	{
		std::atomic<bool> release(false);
		std::atomic<bool> blockerRunning(false);
		std::atomic<bool> secondRan(false);
		Engine::AsyncTask blocker;
		Engine::AsyncTask second;
		blocker.start([&]() {
			blockerRunning.store(true);
			while (!release.load())
				std::this_thread::yield();
		});
		while (!blockerRunning.load())
			std::this_thread::yield();
		second.start([&]() { secondRan.store(true); });
		std::atomic<int> blocks(0);
		Engine::parallelFor(256, 16, [&](std::size_t, std::size_t) { blocks.fetch_add(1); });
		CHECK(blocks.load() == 16);
		CHECK(!secondRan.load());
		release.store(true);
		second.wait();
		CHECK(secondRan.load());
		blocker.wait();
	}

	// shutdown() runs jobs that are still queued.
	{
		std::atomic<bool> secondRan(false);
		Engine::AsyncTask first;
		Engine::AsyncTask second;
		first.start([]() { std::this_thread::sleep_for(std::chrono::milliseconds(30)); });
		second.start([&]() { secondRan.store(true); });
		jobs.shutdown();
		CHECK(secondRan.load());
		CHECK(!first.isRunning());
		CHECK(!second.isRunning());
	}
}

// ------------------------------------------------------------------ Combat model of the new AI
//
// Lanchester forecast and counter scoring on a small hand-made table: tank (armor 0), rocket
// soldier and rifleman (armor 1), gattling (armor 0). Rockets beat tanks per money, gattlings beat
// riflemen; against rocket soldiers a gattling vehicle is a poor buy (they destroy it quickly).
static void testCombatModel()
{
	std::printf("Combat model of the new AI\n");
	auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };

	AICombat::Outcome o = AICombat::lanchester(0.2f, 0.1f);
	CHECK(o.winner == 0);
	CHECK(near(o.survivorsA, std::sqrt(0.5f)));
	CHECK(near(o.seconds, std::atanh(std::sqrt(0.5f)) / std::sqrt(0.02f)));
	CHECK(AICombat::lanchester(0.1f, 0.1f).winner == -1);
	CHECK(AICombat::lanchester(0.0f, 0.0f).winner == -1);
	o = AICombat::lanchester(0.5f, 0.0f);
	CHECK(o.winner == 0 && near(o.seconds, 2.0f) && near(o.survivorsA, 1.0f));
	CHECK(AICombat::lanchester(0.0f, 0.25f).winner == 1);

	AICombat::Table t;
	t.resize(4, 2);
	const int TANK = 0, ROCKET = 1, GATTLING = 2, RIFLE = 3;
	t.profiles[TANK].maxHealth = 400.0f;		t.profiles[TANK].cost = 900.0f;		t.profiles[TANK].armor = 0;
	t.profiles[ROCKET].maxHealth = 100.0f;	t.profiles[ROCKET].cost = 300.0f;	t.profiles[ROCKET].armor = 1;
	t.profiles[GATTLING].maxHealth = 300.0f; t.profiles[GATTLING].cost = 800.0f; t.profiles[GATTLING].armor = 0;
	t.dpsRef(TANK, 0, false) = 40.0f;		t.dpsRef(TANK, 1, false) = 10.0f;
	t.dpsRef(ROCKET, 0, false) = 30.0f;		t.dpsRef(ROCKET, 1, false) = 3.0f;
	t.dpsRef(GATTLING, 0, false) = 5.0f;	t.dpsRef(GATTLING, 1, false) = 50.0f;
	t.profiles[RIFLE].maxHealth = 100.0f;	t.profiles[RIFLE].cost = 200.0f;	t.profiles[RIFLE].armor = 1;
	t.dpsRef(RIFLE, 0, false) = 2.0f;		t.dpsRef(RIFLE, 1, false) = 10.0f;
	CHECK(near(t.dpsAgainst(ROCKET, TANK), 30.0f));
	CHECK(near(t.dpsAgainst(TANK, ROCKET), 10.0f));
	CHECK(t.dpsAgainst(-1, TANK) == 0.0f);

	// Equal forces: nobody wins; one tank more: the square law leaves sqrt(1 - 25/36).
	std::vector<AICombat::Group> five(1), six(1);
	five[0].profile = TANK; five[0].health = 5 * 400.0f;
	six[0].profile = TANK; six[0].health = 6 * 400.0f;
	CHECK(AICombat::predict(t, five, five).winner == -1);
	o = AICombat::predict(t, six, five);
	CHECK(o.winner == 0 && near(o.survivorsA, std::sqrt(1.0f - 25.0f / 36.0f)));
	o = AICombat::predict(t, five, six);
	CHECK(o.winner == 1);
	std::vector<AICombat::Group> none;
	CHECK(AICombat::predict(t, five, none).winner == 0);

	// Counter choice follows the enemy mix.
	std::vector<AICombat::Group> tanks(1), infantry(1);
	tanks[0].profile = TANK; tanks[0].health = 10 * 400.0f;
	infantry[0].profile = RIFLE; infantry[0].health = 20 * 100.0f;
	CHECK(AICombat::counterScore(t, ROCKET, tanks) > AICombat::counterScore(t, GATTLING, tanks));
	CHECK(AICombat::counterScore(t, GATTLING, infantry) > AICombat::counterScore(t, ROCKET, infantry));
	CHECK(AICombat::counterScore(t, GATTLING, infantry) > AICombat::counterScore(t, TANK, infantry));
	std::vector<AICombat::Group> rockets(1);
	rockets[0].profile = ROCKET; rockets[0].health = 20 * 100.0f;
	CHECK(AICombat::counterScore(t, GATTLING, rockets) < AICombat::counterScore(t, GATTLING, infantry));
	CHECK(AICombat::counterScore(t, TANK, none) == 0.0f);

	// Air: a ground-only weapon cannot hurt a flying profile.
	t.profiles[GATTLING].air = true;
	CHECK(t.dpsAgainst(TANK, GATTLING) == 0.0f);
	t.dpsRef(TANK, 0, true) = 7.0f;
	CHECK(near(t.dpsAgainst(TANK, GATTLING), 7.0f));
}

// ------------------------------------------------------------------ Attack planning of the new AI
//
// Influence map basics, then a staged picture: five own tanks; the enemy has an undefended
// building far away and a closer one guarded by turrets and tanks. The plan must pick the
// undefended one, and forecast a lost fight when ten enemy tanks stand next to the force.
static void testAttackPlan()
{
	std::printf("Attack planning of the new AI\n");

	AICombat::InfluenceMap map;
	map.reset(1000.0f, 1000.0f, 100.0f);
	CHECK(map.width == 11 && map.height == 11);
	map.add(550.0f, 550.0f, 0.0f, 3.0f);
	CHECK(map.at(560.0f, 540.0f) == 3.0f);
	CHECK(map.at(150.0f, 150.0f) == 0.0f);
	map.add(550.0f, 550.0f, 220.0f, 1.0f);
	CHECK(map.at(550.0f, 550.0f) == 4.0f);
	CHECK(map.at(350.0f, 550.0f) == 1.0f);		// cell centre 350 is 200 away
	CHECK(map.at(150.0f, 550.0f) == 0.0f);
	CHECK(map.alongLine(50.0f, 550.0f, 1050.0f, 550.0f, 0.0f) > 0.0f);
	CHECK(map.alongLine(50.0f, 550.0f, 1050.0f, 550.0f, 2000.0f) == 0.0f);

	AICombat::Table t;
	t.resize(3, 1);
	const int TANK = 0, BUILDING = 1, TURRET = 2;
	t.profiles[TANK].maxHealth = 400.0f;		t.profiles[TANK].cost = 900.0f;		t.profiles[TANK].armor = 0;		t.profiles[TANK].range = 150.0f;
	t.profiles[BUILDING].maxHealth = 2000.0f;	t.profiles[BUILDING].cost = 1000.0f;	t.profiles[BUILDING].armor = 0;	t.profiles[BUILDING].structure = true;
	t.profiles[TURRET].maxHealth = 1000.0f;	t.profiles[TURRET].cost = 800.0f;		t.profiles[TURRET].armor = 0;	t.profiles[TURRET].structure = true;
	t.profiles[TURRET].range = 300.0f;
	t.dpsRef(TANK, 0, false) = 40.0f;
	t.dpsRef(TURRET, 0, false) = 80.0f;
	CHECK(AICombat::groundThreat(t, TANK) == 40.0f && AICombat::groundThreat(t, BUILDING) == 0.0f);

	std::vector<AICombat::Unit> units;
	auto add = [&](unsigned id, int profile, float x, float y, bool own) {
		AICombat::Unit u;
		u.id = id; u.profile = profile; u.health = t.profiles[profile].maxHealth; u.x = x; u.y = y;
		u.cost = t.profiles[profile].cost; u.own = own; u.enemy = !own; u.focus = !own;
		u.structure = t.profiles[profile].structure;
		units.push_back(u);
	};
	std::vector<unsigned> mission;
	for (unsigned i = 1; i <= 5; ++i)
	{
		add(i, TANK, 1000.0f + 20.0f * i, 1000.0f, true);
		mission.push_back(i);
	}
	add(100, BUILDING, 3000.0f, 1000.0f, false);
	add(101, BUILDING, 1200.0f, 2500.0f, false);
	for (unsigned i = 0; i < 3; ++i)
		add(102 + i, TURRET, 1150.0f + 50.0f * i, 2450.0f, false);
	for (unsigned i = 0; i < 6; ++i)
		add(110 + i, TANK, 1100.0f + 40.0f * i, 2600.0f, false);

	AICombat::AttackPlan plan = AICombat::planAttack(t, 6000.0f, 6000.0f, units, std::vector<unsigned>());
	CHECK(plan.objectiveId == 0);
	CHECK(plan.maxThreat > 0.0f);

	plan = AICombat::planAttack(t, 6000.0f, 6000.0f, units, mission);
	CHECK(plan.objectiveId == 100);
	CHECK(plan.objectiveWinner == 0);
	CHECK(plan.siteEnemyHealth == 0.0f && plan.siteWinner == -1);

	// Ten enemy tanks right next to the force: lost fight, ratio (5/10)^2.
	for (unsigned i = 0; i < 10; ++i)
		add(200 + i, TANK, 1050.0f + 20.0f * i, 1100.0f, false);
	plan = AICombat::planAttack(t, 6000.0f, 6000.0f, units, mission);
	CHECK(plan.siteWinner == 1);
	CHECK(std::fabs(plan.siteRatio - 0.25f) < 0.01f);
	CHECK(plan.siteEnemyHealth > 0.0f);

	// A superweapon building of the focus enemy raises the value of its cell (priority target).
	{
		std::vector<AICombat::Unit> two;
		auto building = [&](unsigned id, float x, bool sw) {
			AICombat::Unit u;
			u.id = id; u.profile = BUILDING; u.health = 2000.0f; u.x = x; u.y = 3000.0f; u.cost = 1000.0f;
			u.enemy = true; u.focus = true; u.structure = true; u.superweapon = sw;
			two.push_back(u);
		};
		for (unsigned i = 1; i <= 5; ++i)
		{
			AICombat::Unit u;
			u.id = i; u.profile = TANK; u.health = 400.0f; u.x = 3000.0f; u.y = 1000.0f; u.own = true;
			two.push_back(u);
		}
		building(300, 2000.0f, false);
		building(301, 4000.0f, true);
		const AICombat::AttackPlan p2 = AICombat::planAttack(t, 6000.0f, 6000.0f, two, mission);
		CHECK(p2.objectiveId == 301);
	}

	// Raid: economic targets only -- the harvester, not the more valuable headquarters next to it.
	{
		std::vector<AICombat::Unit> raid;
		for (unsigned i = 1; i <= 5; ++i)
		{
			AICombat::Unit u;
			u.id = i; u.profile = TANK; u.health = 400.0f; u.x = 1000.0f; u.y = 1000.0f; u.own = true;
			raid.push_back(u);
		}
		AICombat::Unit hq;
		hq.id = 400; hq.profile = BUILDING; hq.health = 2000.0f; hq.x = 2000.0f; hq.y = 1000.0f; hq.cost = 2000.0f;
		hq.enemy = true; hq.focus = true; hq.structure = true;
		raid.push_back(hq);
		AICombat::Unit harvester;
		harvester.id = 401; harvester.profile = BUILDING; harvester.health = 500.0f; harvester.x = 2500.0f; harvester.y = 1500.0f;
		harvester.cost = 600.0f; harvester.enemy = true; harvester.focus = true; harvester.worker = true; harvester.economic = true;
		raid.push_back(harvester);
		AICombat::AttackParams rp;
		CHECK(AICombat::planAttack(t, 6000.0f, 6000.0f, raid, mission, rp).objectiveId == 400);
		rp.economicTargets = true;
		CHECK(AICombat::planAttack(t, 6000.0f, 6000.0f, raid, mission, rp).objectiveId == 401);
	}

	// Strike point: the cluster of value, not the single most expensive object, and never where
	// own objects would be hit harder.
	std::vector<AICombat::StrikePoint> points;
	auto point = [&](unsigned id, float x, float y, float value) {
		AICombat::StrikePoint p;
		p.id = id; p.x = x; p.y = y; p.value = value;
		points.push_back(p);
	};
	point(1, 0.0f, 0.0f, 2000.0f);						// a single expensive building
	for (unsigned i = 0; i < 4; ++i)
		point(10 + i, 1000.0f + 30.0f * i, 1000.0f, 800.0f);	// a group worth 3200
	AICombat::StrikeChoice c = AICombat::bestStrike(points, 150.0f);
	CHECK(c.found && c.id >= 10 && c.id <= 13 && std::fabs(c.value - 3200.0f) < 0.5f);
	point(50, 1040.0f, 1000.0f, -8000.0f);			// own units in the middle of that group
	c = AICombat::bestStrike(points, 150.0f);
	CHECK(c.found && c.id == 1);
	std::vector<AICombat::StrikePoint> onlyOwn(1);
	onlyOwn[0].value = -100.0f;
	CHECK(!AICombat::bestStrike(onlyOwn, 150.0f).found);

	// Strike timing: a rich target at once, otherwise with the own attack or after the longest wait.
	AICombat::StrikeTiming st;
	CHECK(AICombat::fireStrikeNow(3200.0f, false, 0, st));
	CHECK(!AICombat::fireStrikeNow(1500.0f, false, 0, st));
	CHECK(AICombat::fireStrikeNow(1500.0f, true, 0, st));
	CHECK(!AICombat::fireStrikeNow(1500.0f, false, st.maxHoldFrames - 1, st));
	CHECK(AICombat::fireStrikeNow(1500.0f, false, st.maxHoldFrames, st));
}

// ------------------------------------------------------------------ Fight control of the new AI
//
// Focus fire takes the target that hurts most per point of health first and stops adding shooters
// once the target dies within the kill time; keeping distance steps back from a short-ranged
// threat only; spreading puts a clumped group on rings with the requested spacing.
static void testFightControl()
{
	std::printf("Fight control of the new AI\n");
	AICombat::Table t;
	t.resize(3, 1);
	const int TANK = 0, ARTY = 1, TRUCK = 2;
	t.profiles[TANK].maxHealth = 400.0f;	t.profiles[TANK].cost = 900.0f;	t.profiles[TANK].armor = 0;	t.profiles[TANK].range = 150.0f;
	t.profiles[ARTY].maxHealth = 200.0f;	t.profiles[ARTY].cost = 1000.0f;	t.profiles[ARTY].armor = 0;	t.profiles[ARTY].range = 400.0f;
	t.profiles[ARTY].splash = 50.0f;
	t.profiles[TRUCK].maxHealth = 300.0f;	t.profiles[TRUCK].cost = 600.0f;	t.profiles[TRUCK].armor = 0;
	t.dpsRef(TANK, 0, false) = 40.0f;
	t.dpsRef(ARTY, 0, false) = 60.0f;

	auto fighter = [](unsigned id, int profile, float health, float x, float y) {
		AICombat::Fighter f;
		f.id = id; f.profile = profile; f.health = health; f.x = x; f.y = y;
		return f;
	};
	std::vector<AICombat::Fighter> own;
	for (unsigned i = 0; i < 6; ++i)
		own.push_back(fighter(1 + i, TANK, 400.0f, 1000.0f + 10.0f * i, 1000.0f));
	std::vector<AICombat::Fighter> enemies;
	enemies.push_back(fighter(50, TRUCK, 300.0f, 1100.0f, 1000.0f));		// harmless
	enemies.push_back(fighter(51, TANK, 400.0f, 1120.0f, 1000.0f));		// healthy tank
	enemies.push_back(fighter(52, TANK, 100.0f, 1130.0f, 1000.0f));		// damaged tank: first
	enemies.push_back(fighter(53, TANK, 400.0f, 5000.0f, 5000.0f));		// out of reach
	std::vector<AICombat::FocusAssignment> a = AICombat::assignFocusFire(t, own, enemies, 100.0f, 4.0f);
	CHECK(a.size() == 6);
	// Tank 52 needs 100/4 = 25 damage per second: one shooter (40) is enough.
	CHECK(a[0].shooter == 1 && a[0].target == 52);
	// Tank 51 needs 100: three shooters (120); the remaining two take the truck.
	int on51 = 0, on50 = 0, on53 = 0;
	for (const AICombat::FocusAssignment &x : a)
	{
		on51 += (x.target == 51) ? 1 : 0;
		on50 += (x.target == 50) ? 1 : 0;
		on53 += (x.target == 53) ? 1 : 0;
	}
	CHECK(on51 == 3 && on50 == 2 && on53 == 0);
	CHECK(AICombat::assignFocusFire(t, own, std::vector<AICombat::Fighter>()).empty());

	// Artillery (range 400) steps back from a tank (range 150) standing 140 away; not from one 300 away.
	float x = 0.0f, y = 0.0f;
	AICombat::Fighter arty = fighter(9, ARTY, 200.0f, 1000.0f, 1000.0f);
	std::vector<AICombat::Fighter> close(1, fighter(60, TANK, 400.0f, 1140.0f, 1000.0f));
	CHECK(AICombat::kiteStep(t, arty, close, &x, &y));
	CHECK(std::fabs(x - (1140.0f - 360.0f)) < 0.5f && std::fabs(y - 1000.0f) < 0.5f);
	std::vector<AICombat::Fighter> distant(1, fighter(61, TANK, 400.0f, 1300.0f, 1000.0f));
	CHECK(!AICombat::kiteStep(t, arty, distant, &x, &y));
	// A tank does not run from a tank (same range).
	CHECK(!AICombat::kiteStep(t, own[0], close, &x, &y));
	// Speeds known: slow artillery keeps firing at a clearly faster tank; at a slower one it steps back.
	AICombat::Fighter slowArty = arty;
	slowArty.speed = 20.0f;
	std::vector<AICombat::Fighter> fast = close;
	fast[0].speed = 30.0f;
	CHECK(!AICombat::kiteStep(t, slowArty, fast, &x, &y));
	fast[0].speed = 21.0f;		// within 10 %
	CHECK(AICombat::kiteStep(t, slowArty, fast, &x, &y));
	fast[0].speed = 0.0f;		// unknown: as before
	CHECK(AICombat::kiteStep(t, slowArty, fast, &x, &y));

	// Seven tanks in a clump get spread on rings 0 and 1 with spacing 80.
	std::vector<AICombat::Fighter> clump;
	for (unsigned i = 0; i < 7; ++i)
		clump.push_back(fighter(100 + i, TANK, 400.0f, 2000.0f + 5.0f * (i % 3), 2000.0f + 5.0f * (i / 3)));
	CHECK(AICombat::isClumped(clump, 80.0f));
	std::vector<AICombat::Fighter> spread = AICombat::spreadPositions(clump, 80.0f);
	CHECK(spread.size() == 7);
	CHECK(!AICombat::isClumped(spread, 80.0f));
	float minD2 = 1.0e9f;
	for (size_t i = 0; i < spread.size(); ++i)
		for (size_t j = i + 1; j < spread.size(); ++j)
			minD2 = std::min(minD2, AICombat::distance2(spread[i], spread[j]));
	CHECK(std::sqrt(minD2) >= 79.0f);
}

// ------------------------------------------------------------------ Texture tiers

static void testTextureTier()
{
	std::printf("TextureTier\n");
	using namespace Engine;
	CHECK(parseTextureTier("Auto") == TEXTURE_TIER_AUTO);
	CHECK(parseTextureTier("sd") == TEXTURE_TIER_SD);
	CHECK(parseTextureTier("HD") == TEXTURE_TIER_HD);
	CHECK(parseTextureTier("Uhd") == TEXTURE_TIER_UHD);
	CHECK(parseTextureTier("4K") == TEXTURE_TIER_UHD);
	CHECK(parseTextureTier("8k") == TEXTURE_TIER_INVALID);
	CHECK(parseTextureTier("") == TEXTURE_TIER_INVALID);
	CHECK(parseTextureTier(nullptr) == TEXTURE_TIER_INVALID);
	CHECK(std::strcmp(textureTierName(TEXTURE_TIER_UHD), "UHD") == 0);
	CHECK(textureTierFolder(TEXTURE_TIER_SD) == nullptr);
	CHECK(std::strcmp(textureTierFolder(TEXTURE_TIER_HD), "HD") == 0);

	const std::uint64_t GB = 1024ull * 1024ull * 1024ull;
	TextureTierInput in;
	in.available[TEXTURE_TIER_HD] = true;
	in.available[TEXTURE_TIER_UHD] = true;

	// Rule 1: output height
	in.outputHeight = 720;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_SD);
	in.outputHeight = 900;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_HD);
	in.outputHeight = 1440;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_HD);
	in.outputHeight = 2160;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_UHD);

	// Rule 3: UMA at most HD
	in.unifiedMemory = true;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_HD);
	in.unifiedMemory = false;

	// Rule 2: budget with a 1.5 margin
	in.demandBytes[TEXTURE_TIER_SD] = GB / 4;
	in.demandBytes[TEXTURE_TIER_HD] = GB;
	in.demandBytes[TEXTURE_TIER_UHD] = 3 * GB;
	in.budgetBytes = 4 * GB;			// 3 GB * 1.5 = 4.5 GB > 4 GB
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_HD);
	in.budgetBytes = 5 * GB;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_UHD);
	in.budgetBytes = GB;				// HD does not fit either
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_SD);
	in.budgetBytes = 0;					// unknown: no step down
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_UHD);

	// Rule 0: detail level Low means SD, whatever height and memory
	in.lowDetail = true;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_SD);
	CHECK(std::strcmp(chooseTextureTier(in).reason, "detail level Low") == 0);
	in.requested = TEXTURE_TIER_HD;		// an explicit choice wins
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_HD);
	in.requested = TEXTURE_TIER_AUTO;
	in.lowDetail = false;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_UHD);

	// An explicit choice skips height, UMA and budget
	in.requested = TEXTURE_TIER_UHD;
	in.outputHeight = 600;
	in.unifiedMemory = true;
	in.budgetBytes = GB;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_UHD);
	in.requested = TEXTURE_TIER_SD;
	in.outputHeight = 2160;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_SD);

	// Rule 4: missing tiers fall back to the next lower one
	in.requested = TEXTURE_TIER_UHD;
	in.available[TEXTURE_TIER_UHD] = false;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_HD);
	in.available[TEXTURE_TIER_HD] = false;
	CHECK(chooseTextureTier(in).tier == TEXTURE_TIER_SD);
	CHECK(std::strlen(chooseTextureTier(in).reason) > 0);

	// Detail level for the first start: integrated graphics Low, dedicated cards High or VeryHigh, no adapter VeryHigh
	{
		VideoMemoryInfo info;
		CHECK(recommendDetailLevel(info).level == 3);
		info.valid = true;
		info.dedicatedVideoMemory = 128ull << 20;			// Intel HD: BIOS share only
		CHECK(recommendDetailLevel(info).level == 0);
		info.dedicatedVideoMemory = 8ull << 30;
		info.umaKnown = true;
		info.unifiedMemory = true;							// UMA wins over a large reported size
		CHECK(recommendDetailLevel(info).level == 0);
		info.unifiedMemory = false;
		CHECK(recommendDetailLevel(info).level == 3);
		info.dedicatedVideoMemory = 4ull << 30;
		CHECK(recommendDetailLevel(info).level == 2);
		info.dedicatedVideoMemory = DETAIL_LOW_BELOW_BYTES - 1;
		CHECK(recommendDetailLevel(info).level == 0);
		info.dedicatedVideoMemory = DETAIL_VERY_HIGH_FROM_BYTES;
		CHECK(recommendDetailLevel(info).level == 3);
		CHECK(std::strlen(recommendDetailLevel(info).reason) > 0);
	}

	// Video memory query: must not fail without an adapter, same object twice
	const VideoMemoryInfo &vm = VideoMemoryInfo::get();
	CHECK(&vm == &VideoMemoryInfo::get());
	char text[256];
	vm.describe(text, sizeof(text));
	CHECK(text[0] != 0);
	std::printf("  video memory: %s\n", text);
}

// Files under the user data folder: narrow paths, wildcards, writing in one step, names outside ASCII.
static void testUserDataFiles()
{
	std::printf("UserDataFiles\n");
	using namespace Engine;
	namespace fs = std::filesystem;

	CHECK(!isUtf8WithNonAscii("Save\\00000001.sav"));
	CHECK(isUtf8WithNonAscii("Saves\xC3\xA4"));
	CHECK(isUtf8WithNonAscii("\xE6\x97\xA5\xE6\x9C\xAC"));
	CHECK(!isUtf8WithNonAscii("Saves\xE4"));			// CP1252
	CHECK(!isUtf8WithNonAscii("\xC0\xAF"));					// overlong
	CHECK(!isUtf8WithNonAscii("\xED\xA0\x80"));				// surrogate
	CHECK(!isUtf8WithNonAscii("\xE6\x97"));					// cut off
	CHECK(!isUtf8WithNonAscii("\xF4\x90\x80\x80"));			// past U+10FFFF

	CHECK(matchesWildcard("00000001.SAV", "*.sav"));
	CHECK(matchesWildcard("a.sav", "*"));
	CHECK(!matchesWildcard("a.sav.tmp", "*.sav"));
	CHECK(matchesWildcard("Replay1.rep", "replay?.*"));
	CHECK(!matchesWildcard("Replay12.rep", "replay?.rep"));
	CHECK(matchesWildcard("", "*"));
	CHECK(!matchesWildcard("x", ""));

	const fs::path unicodeName = pathFromNarrow("\xE6\x97\xA5\xE6\x9C\xAC_\xC3\xA4");
	CHECK(pathFromNarrow(narrowFromPath(unicodeName)) == unicodeName);
	CHECK(narrowFromPath(pathFromNarrow("Save\\a.sav")) == "Save\\a.sav");

	std::error_code ec;
	const fs::path base = fs::temp_directory_path(ec) / ("enginetests_userdata_" + std::to_string(std::rand()));
	fs::remove_all(base, ec);
	const fs::path root = base / unicodeName;
	CHECK(UserDataFiles::createDirectories(root / "Save"));
	UserDataFiles::setRoot(root);
	CHECK(UserDataFiles::root() == root);
	const fs::path save = UserDataFiles::resolve("Save\\a.sav");
	CHECK(save == root / "Save" / "a.sav");

	std::string error;
	CHECK(UserDataFiles::writeAtomically(save, "first", &error));
	CHECK(UserDataFiles::writeAtomically(save, "second version", &error));
	std::string bytes;
	CHECK(UserDataFiles::readFile(save, bytes) && bytes == "second version");
	fs::path temporary = save;
	temporary += ".tmp";
	CHECK(!UserDataFiles::exists(temporary));
	// a failed write leaves the old file
	CHECK(!UserDataFiles::writeAtomically(root / "missing" / "b.sav", "x", &error) && !error.empty());

	CHECK(UserDataFiles::writeAtomically(root / "Save" / "b.SAV", std::string(1000, 'b')));
	CHECK(UserDataFiles::writeAtomically(root / "Save" / pathFromNarrow("\xC3\xA4.sav"), "c"));
	CHECK(UserDataFiles::writeAtomically(root / "Save" / "c.txt", "c"));
	CHECK(UserDataFiles::createDirectories(root / "Save" / "Folder.sav"));
	std::vector<UserDataEntry> entries = UserDataFiles::list(root / "Save", "*.sav");
	std::sort(entries.begin(), entries.end(), [](const UserDataEntry &a, const UserDataEntry &b) { return a.name < b.name; });
	CHECK(entries.size() == 4);
	if (entries.size() == 4)
	{
		CHECK(entries[0].name == "Folder.sav" && entries[0].directory);
		CHECK(entries[1].name == "a.sav" && !entries[1].directory && entries[1].size == 14);
		CHECK(entries[2].name == "b.SAV" && entries[2].size == 1000);
		CHECK(pathFromNarrow(entries[3].name) == pathFromNarrow("\xC3\xA4.sav"));
	}
	CHECK(UserDataFiles::list(root / "missing", "*").empty());

	const fs::path copy = root / "Copy.sav";
	CHECK(UserDataFiles::copyFile(save, copy, false));
	CHECK(!UserDataFiles::copyFile(save, copy, false));		// exists, no overwrite
	CHECK(UserDataFiles::copyFile(root / "Save" / "b.SAV", copy, true));
	CHECK(UserDataFiles::readFile(copy, bytes) && bytes.size() == 1000);
	CHECK(UserDataFiles::replaceFile(copy, save));
	CHECK(!UserDataFiles::exists(copy) && UserDataFiles::readFile(save, bytes) && bytes.size() == 1000);

	std::FILE *file = openNarrowFile(narrowFromPath(root / "open.txt"), "wb");
	CHECK(file != nullptr);
	if (file)
		std::fclose(file);
	CHECK(UserDataFiles::exists(root / "open.txt"));
	CHECK(UserDataFiles::remove(root / "open.txt"));
	CHECK(UserDataFiles::remove(root / "open.txt"));		// already gone
	CHECK(!UserDataFiles::isDirectory(save) && UserDataFiles::isDirectory(root / "Save"));

	UserDataFiles::clearRoot();
	CHECK(UserDataFiles::root().empty());
	fs::remove_all(base, ec);
}

// One texture package: the tier picks the mip offset of each texture from its original size.
static void testTexturePackage()
{
	std::printf("TexturePackage\n");
	using namespace Engine;
	CHECK(textureTierScale(TEXTURE_TIER_SD) == 1);
	CHECK(textureTierScale(TEXTURE_TIER_HD) == 2);
	CHECK(textureTierScale(TEXTURE_TIER_UHD) == 4);

	// mip offset: original 256 x 128, package at UHD (1024 x 512, 11 levels)
	CHECK(textureMipOffset(1024, 512, 256, 128, 1, 11) == 2);
	CHECK(textureMipOffset(1024, 512, 256, 128, 2, 11) == 1);
	CHECK(textureMipOffset(1024, 512, 256, 128, 4, 11) == 0);
	// file only at HD: UHD loads the file as it is
	CHECK(textureMipOffset(512, 256, 256, 128, 4, 10) == 0);
	CHECK(textureMipOffset(512, 256, 256, 128, 1, 10) == 1);
	// no edge ends up below the target, not even with a different aspect ratio
	CHECK(textureMipOffset(1024, 256, 256, 128, 1, 11) == 1);
	// not a power of two
	CHECK(textureMipOffset(400, 400, 100, 100, 1, 9) == 2);
	// a single level or unknown size: nothing to skip; the last level is never skipped
	CHECK(textureMipOffset(1024, 1024, 256, 256, 1, 1) == 0);
	CHECK(textureMipOffset(1024, 1024, 0, 256, 1, 11) == 0);
	CHECK(textureMipOffset(1024, 1024, 1, 1, 1, 3) == 2);

	CHECK(texturePackageKey("Art\\Textures\\ABC_d.dds") == "abc_d");
	CHECK(texturePackageKey("data/english/art/textures/x.y.tga") == "x.y");
	CHECK(texturePackageKey("plain") == "plain");
	CHECK(texturePackageKey(nullptr).empty());

	TexturePackage pkg;
	CHECK(!pkg.active());
	CHECK(pkg.addManifest("no manifest", 11) == -1);
	CHECK(pkg.addManifest("TEXTURPAKET 2 UHD\n", 18) == -1);
	CHECK(!pkg.active());
	const char manifest[] =
		"\r\nTEXTURPAKET 1 UHD\r\n"
		"; comment\r\n"
		"art\\textures\\abc.dds 256 128\r\n"
		"art\\terrain\\grass.dds 64 64\n"
		"art\\textures\\bad.dds 0 64\n"
		"art\\textures\\bad2.dds 64\n"
		"art\\textures\\bad3.dds 64 64 7\n"
		"art\\textures\\gas can.dds 64 32\n"
		"\n";
	CHECK(pkg.addManifest(manifest, sizeof(manifest) - 1, "Texturstufen\\Paket\\TexturesPaket.big") == 3);
	CHECK(pkg.active());
	CHECK(pkg.size() == 3);
	CHECK(pkg.badLines() == 3);
	CHECK(pkg.highestTier() == TEXTURE_TIER_UHD);
	unsigned w = 0, h = 0;
	CHECK(pkg.logicalSize("ABC.tga", w, h) && w == 256 && h == 128);
	CHECK(!pkg.logicalSize("unknown.tga", w, h));
	CHECK(pkg.logicalSize("Gas Can.tga", w, h) && w == 64 && h == 32);

	// a second archive: known entries keep their first size, the highest tier is the lowest named
	const char second[] = "TEXTURPAKET 1 HD\nart\\textures\\abc.dds 512 512\nart\\textures\\neu.dds 32 32\n";
	CHECK(pkg.addManifest(second, sizeof(second) - 1, "ProjectData\\Inhalt\\Frei\\") == 1);
	CHECK(pkg.logicalSize("abc", w, h) && w == 256 && h == 128);
	CHECK(pkg.highestTier() == TEXTURE_TIER_HD);

	// each source knows the textures its own manifest names, also those another source named first
	CHECK(pkg.provides("texturstufen\\paket\\texturespaket.BIG", "Art\\Textures\\ABC.tga"));
	CHECK(pkg.provides("ProjectData\\Inhalt\\Frei\\", "abc.dds") && pkg.provides("ProjectData\\Inhalt\\Frei\\", "neu"));
	CHECK(!pkg.provides("ProjectData\\Inhalt\\Frei\\", "grass.dds"));
	CHECK(!pkg.provides("Mod\\", "abc.dds"));
	CHECK(pkg.namedBy("Texturstufen\\Paket\\TexturesPaket.big") == 3 && pkg.namedBy("ProjectData\\Inhalt\\Frei\\") == 2);
	CHECK(pkg.namedBy("Mod\\") == -1);

	pkg.setTier(TEXTURE_TIER_SD);
	CHECK(pkg.mipOffset("abc.tga", 1024, 512, 11) == 2);
	pkg.setTier(TEXTURE_TIER_HD);
	CHECK(pkg.mipOffset("abc.tga", 1024, 512, 11) == 1);
	CHECK(pkg.mipOffset("unknown.tga", 1024, 512, 11) == 0);
	CHECK(pkg.scaleFactor("abc.dds", 512) == 2.0f);
	CHECK(pkg.scaleFactor("unknown.dds", 512) == 1.0f);

	// paths as the first manifest names them; a replaced texture loses its entry
	std::vector<std::string> paths = pkg.paths();
	std::sort(paths.begin(), paths.end());
	CHECK(paths.size() == 4 && paths[0] == "art\\terrain\\grass.dds" && paths[1] == "art\\textures\\abc.dds"
		&& paths[2] == "art\\textures\\gas can.dds" && paths[3] == "art\\textures\\neu.dds");
	CHECK(pkg.forget("Art\\Textures\\ABC.tga"));
	CHECK(!pkg.forget("abc"));
	CHECK(pkg.mipOffset("abc.tga", 1024, 512, 11) == 0);
	CHECK(pkg.scaleFactor("abc.dds", 512) == 1.0f);
	CHECK(pkg.size() == 3 && pkg.active());

	pkg.clear();
	CHECK(!pkg.active() && pkg.size() == 0);
	CHECK(!pkg.provides("ProjectData\\Inhalt\\Frei\\", "neu") && pkg.namedBy("ProjectData\\Inhalt\\Frei\\") == -1);
}

// ------------------------------------------------------------------ Keyboard without DirectInput

// Mapping Windows scan code -> internal key code (KeyScanCode.h). The key codes are the
// DirectInput values (KeyDefs.h); the comparison with dinput.h itself is in UnitTests
// (KeyDefs_DInput) because it needs Windows headers.
static void testKeyScanCode()
{
	std::printf("KeyScanCode\n");
	using KeyScan::keyFromScanCode;

	// the keys bound in the game and in the test helpers
	CHECK(keyFromScanCode(0x01, false) == KEY_ESC);
	CHECK(keyFromScanCode(0x1E, false) == KEY_A);
	CHECK(keyFromScanCode(0x2C, false) == KEY_Z);
	CHECK(keyFromScanCode(0x02, false) == KEY_1);
	CHECK(keyFromScanCode(0x0B, false) == KEY_0);
	CHECK(keyFromScanCode(0x3B, false) == KEY_F1);
	CHECK(keyFromScanCode(0x44, false) == KEY_F10);
	CHECK(keyFromScanCode(0x57, false) == KEY_F11);
	CHECK(keyFromScanCode(0x58, false) == KEY_F12);
	CHECK(keyFromScanCode(0x0F, false) == KEY_TAB);
	CHECK(keyFromScanCode(0x39, false) == KEY_SPACE);
	CHECK(keyFromScanCode(0x0E, false) == KEY_BACKSPACE);
	CHECK(keyFromScanCode(0x1C, false) == KEY_ENTER);
	CHECK(keyFromScanCode(0x29, false) == KEY_TICK);
	CHECK(keyFromScanCode(0x56, false) == KEY_102);			// < > | on German keyboards

	// Modifiers left/right
	CHECK(keyFromScanCode(0x2A, false) == KEY_LSHIFT);
	CHECK(keyFromScanCode(0x36, false) == KEY_RSHIFT);
	CHECK(keyFromScanCode(0x1D, false) == KEY_LCTRL);
	CHECK(keyFromScanCode(0x1D, true) == KEY_RCTRL);
	CHECK(keyFromScanCode(0x38, false) == KEY_LALT);
	CHECK(keyFromScanCode(0x38, true) == KEY_RALT);
	CHECK(keyFromScanCode(0x3A, false) == KEY_CAPS);
	CHECK(keyFromScanCode(0x2A, true) == KEY_NONE);			// fake shift E0 2A
	CHECK(keyFromScanCode(0x36, true) == KEY_NONE);

	// Arrows, block above the arrows, numpad (with and without E0)
	CHECK(keyFromScanCode(0x48, true) == KEY_UP);
	CHECK(keyFromScanCode(0x50, true) == KEY_DOWN);
	CHECK(keyFromScanCode(0x4B, true) == KEY_LEFT);
	CHECK(keyFromScanCode(0x4D, true) == KEY_RIGHT);
	CHECK(keyFromScanCode(0x47, true) == KEY_HOME);
	CHECK(keyFromScanCode(0x4F, true) == KEY_END);
	CHECK(keyFromScanCode(0x49, true) == KEY_PGUP);
	CHECK(keyFromScanCode(0x51, true) == KEY_PGDN);
	CHECK(keyFromScanCode(0x52, true) == KEY_INS);
	CHECK(keyFromScanCode(0x53, true) == KEY_DEL);
	CHECK(keyFromScanCode(0x48, false) == KEY_KP8);
	CHECK(keyFromScanCode(0x52, false) == KEY_KP0);
	CHECK(keyFromScanCode(0x53, false) == KEY_KPDEL);
	CHECK(keyFromScanCode(0x37, false) == KEY_KPSTAR);
	CHECK(keyFromScanCode(0x4A, false) == KEY_KPMINUS);
	CHECK(keyFromScanCode(0x4E, false) == KEY_KPPLUS);
	CHECK(keyFromScanCode(0x1C, true) == KEY_KPENTER);
	CHECK(keyFromScanCode(0x35, true) == KEY_KPSLASH);
	CHECK(keyFromScanCode(0x35, false) == KEY_SLASH);

	// Special cases: Num/Pause, Scroll/Break, Print
	CHECK(keyFromScanCode(0x45, true) == KEY_NUM);
	CHECK(keyFromScanCode(0x45, false) == 0xC5);				// DIK_PAUSE (no KEY_ name)
	CHECK(keyFromScanCode(0x46, false) == KEY_SCROLL);
	CHECK(keyFromScanCode(0x46, true) == 0xC5);
	CHECK(keyFromScanCode(0x37, true) == KEY_SYSREQ);
	CHECK(keyFromScanCode(0x54, false) == KEY_SYSREQ);

	// invalid: no scan code, release code, only the low byte counts
	CHECK(keyFromScanCode(0x00, false) == KEY_NONE);
	CHECK(keyFromScanCode(0x9E, false) == KEY_NONE);
	CHECK(keyFromScanCode(0x11E, false) == KEY_A);
	CHECK(keyFromScanCode(0x7F, true) == KEY_NONE);			// would be 0xFF = KEY_LOST

	// Whole table: except for special cases "scan code | 0x80 with E0", never KEY_LOST (0xFF)
	for (unsigned sc = 1; sc < 0x80; sc++)
	{
		for (int ext = 0; ext < 2; ext++)
		{
			const unsigned got = keyFromScanCode(sc, ext != 0);
			const bool special = sc == 0x45 || sc == 0x54 || (ext && (sc == 0x2A || sc == 0x36 || sc == 0x46 || sc == 0x7F));
			if (!special)
				CHECK_SILENT(got == (ext ? (sc | 0x80u) : sc));
			CHECK_SILENT(got != KEY_LOST);
		}
	}
}

// A .big archive in memory: header, directory, then filler up to dataSize bytes of file data.
static std::vector<unsigned char> makeBigArchive(const std::vector<Engine::BigArchiveEntry> &entries, std::size_t dataSize)
{
	auto be = [](std::vector<unsigned char> &v, std::uint32_t x)
	{
		for (int shift = 24; shift >= 0; shift -= 8)
			v.push_back(static_cast<unsigned char>(x >> shift));
	};
	std::vector<unsigned char> v = {'B', 'I', 'G', 'F', 0, 0, 0, 0};
	be(v, static_cast<std::uint32_t>(entries.size()));
	be(v, 0);
	for (const Engine::BigArchiveEntry &e : entries)
	{
		be(v, e.offset);
		be(v, e.size);
		v.insert(v.end(), e.path.begin(), e.path.end());
		v.push_back(0);
	}
	v.resize(v.size() + dataSize, 0xAB);
	return v;
}

static Engine::BigArchiveError readBig(const std::vector<unsigned char> &archive, std::vector<Engine::BigArchiveEntry> &entries,
	std::size_t chunk = 1u << 20)
{
	std::size_t pos = 0;
	Engine::BigArchiveRead read = [&](void *dest, std::size_t size) -> std::size_t
	{
		std::size_t n = std::min({size, chunk, archive.size() - pos});
		std::memcpy(dest, archive.data() + pos, n);
		pos += n;
		return n;
	};
	return Engine::readBigArchiveDirectory(read, archive.size(), entries);
}

static void testBigArchive()
{
	using namespace Engine;
	std::printf("BigArchive\n");
	std::vector<BigArchiveEntry> entries;

	std::vector<BigArchiveEntry> in = {{"data\\ini\\a.ini", 60, 10}, {"maps\\x\\x.map", 70, 30}};
	std::vector<unsigned char> archive = makeBigArchive(in, 100);
	CHECK(readBig(archive, entries) == BigArchiveError::None);
	CHECK(entries.size() == 2 && entries[1].path == "maps\\x\\x.map" && entries[1].offset == 70 && entries[1].size == 30);
	// one byte per read call gives the same result
	CHECK(readBig(archive, entries, 1) == BigArchiveError::None && entries.size() == 2);

	std::vector<unsigned char> bad = archive;
	bad[3] = '4';
	CHECK(readBig(bad, entries) == BigArchiveError::NotBig);
	CHECK(readBig(std::vector<unsigned char>(archive.begin(), archive.begin() + 10), entries) == BigArchiveError::TruncatedHeader);

	bad = archive;
	bad[8] = 0x7F;	// entry count far beyond what the archive holds
	CHECK(readBig(bad, entries) == BigArchiveError::BadEntryCount);

	// directory cut off inside the second name
	std::vector<unsigned char> empty = makeBigArchive({{"data\\ini\\a.ini", 0, 0}, {"maps\\x\\x.map", 0, 0}}, 0);
	CHECK(readBig(std::vector<unsigned char>(empty.begin(), empty.end() - 3), entries) == BigArchiveError::TruncatedEntry);

	CHECK(readBig(makeBigArchive({{std::string(BIG_ARCHIVE_MAX_NAME, 'n'), 0, 0}}, 0), entries) == BigArchiveError::None);
	CHECK(readBig(makeBigArchive({{std::string(BIG_ARCHIVE_MAX_NAME + 1, 'n'), 0, 0}}, 0), entries) == BigArchiveError::NameTooLong);

	std::vector<unsigned char> small = makeBigArchive({{"a", 0, 0}}, 10);
	const std::uint32_t end = static_cast<std::uint32_t>(small.size());
	CHECK(readBig(makeBigArchive({{"a", end - 10, 10}}, 10), entries) == BigArchiveError::None);
	CHECK(readBig(makeBigArchive({{"a", end - 10, 11}}, 10), entries) == BigArchiveError::EntryOutsideArchive);
	CHECK(readBig(makeBigArchive({{"a", 0xFFFFFFF0u, 0x20}}, 10), entries) == BigArchiveError::EntryOutsideArchive);
	CHECK(std::strcmp(bigArchiveErrorText(BigArchiveError::NameTooLong), "entry name too long") == 0);
}

// A .csf file in memory: header, then per label its strings (stored inverted, as the game does).
struct CsfLabel { std::string label; std::u16string text; std::string speech; };

static std::vector<unsigned char> makeCsf(const std::vector<CsfLabel> &labels, std::uint32_t announced)
{
	std::vector<unsigned char> v;
	auto u32 = [&](std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<unsigned char>(x >> (8 * i))); };
	auto tag = [&](const char *t) { v.push_back(t[3]); v.push_back(t[2]); v.push_back(t[1]); v.push_back(t[0]); };
	tag("CSF "); u32(3); u32(announced); u32(announced); u32(0); u32(7);
	for (const CsfLabel &l : labels)
	{
		tag("LBL "); u32(1); u32(static_cast<std::uint32_t>(l.label.size()));
		v.insert(v.end(), l.label.begin(), l.label.end());
		tag(l.speech.empty() ? "STR " : "STRW"); u32(static_cast<std::uint32_t>(l.text.size()));
		for (char16_t c : l.text) { const char16_t x = static_cast<char16_t>(~c); v.push_back(x & 0xFF); v.push_back(x >> 8); }
		if (!l.speech.empty()) { u32(static_cast<std::uint32_t>(l.speech.size())); v.insert(v.end(), l.speech.begin(), l.speech.end()); }
	}
	return v;
}

static void testWheelSteps()
{
	std::printf("WheelSteps\n");
	int rest = 0;
	CHECK(Engine::takeWheelNotches(rest, 120) == 1 && rest == 0);		// classic wheel: one notch per event
	CHECK(Engine::takeWheelNotches(rest, -240) == -2 && rest == 0);
	CHECK(Engine::takeWheelNotches(rest, 30) == 0 && rest == 30);		// high-resolution steps add up
	CHECK(Engine::takeWheelNotches(rest, 30) == 0 && rest == 60);
	CHECK(Engine::takeWheelNotches(rest, 70) == 1 && rest == 10);
	CHECK(Engine::takeWheelNotches(rest, -40) == 0 && rest == -40);		// turning back drops the rest
	CHECK(Engine::takeWheelNotches(rest, -80) == -1 && rest == 0);
	int sum = 0;
	rest = 0;
	for (int i = 0; i < 48; ++i)										// 48 steps of 15 = 6 notches
		sum += Engine::takeWheelNotches(rest, 15);
	CHECK(sum == 6 && rest == 0);
}

// ------------------------------------------------------------------ CRC queue of the replay playback
static void testReplayCrcQueue()
{
	std::printf("ReplayCrcQueue\n");
	// recomputed CRCs every frame (interval 1), arriving one frame after their logic frame; frames 0 and 1 alike
	const std::uint32_t crc[] = { 0xA0, 0xA0, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x17, 0x28, 0x39, 0x4A, 0x5B };
	const std::uint32_t count = sizeof(crc) / sizeof(crc[0]);
	// plays a recording that lacks the first `missing` CRCs; returns the frame of the first mismatch, or 0
	auto play = [&](std::size_t missing, std::uint32_t corruptAt, std::size_t &offset)
	{
		Engine::ReplayCrcQueue q;
		std::size_t next = missing;
		std::uint32_t mismatch = 0;
		for (std::uint32_t frame = 1; frame <= count + 3; ++frame)
		{
			if (frame <= count)
				q.addComputed(crc[frame - 1], frame);
			// the recorded CRC of logic frame g is executed some frames later (run-ahead 3)
			if (frame >= 4 && next < count && next + 4 <= frame)
			{
				std::uint32_t value = crc[next];
				if (next == corruptAt)
					value ^= 1;
				std::uint32_t expected = 0;
				if (!q.checkRecorded(value, expected) && mismatch == 0)
					mismatch = static_cast<std::uint32_t>(next) + 1;
				++next;
			}
		}
		offset = q.aligned() ? q.offset() : 99;
		return mismatch;
	};
	std::size_t offset = 0;
	CHECK(play(0, 99, offset) == 0 && offset == 0);		// everything recorded (single player)
	CHECK(play(1, 99, offset) == 0 && offset == 1);		// CRC of frame 0 missing, despite equal values of frames 0 and 1
	CHECK(play(2, 99, offset) == 0 && offset == 2);		// CRCs of frames 0 and 1 missing
	CHECK(play(2, 6, offset) == 7);						// a real difference later is still found
	CHECK(play(0, 0, offset) != 0);						// and one at the very start
	CHECK(play(2, 2, offset) != 0);
	// more than two missing CRCs is not a pregame gap: reported, not skipped
	CHECK(play(3, 99, offset) != 0);
	// interval 100: only the CRC of frame 0 arrives in a pregame frame
	{
		Engine::ReplayCrcQueue q;
		q.addComputed(0x11, 1);
		q.addComputed(0x22, 101);
		q.addComputed(0x33, 201);
		std::uint32_t expected = 0;
		CHECK(q.checkRecorded(0x22, expected) && q.aligned() && q.offset() == 1);
		CHECK(q.checkRecorded(0x33, expected));
		Engine::ReplayCrcQueue solo;
		solo.addComputed(0x11, 1);
		solo.addComputed(0x22, 101);
		CHECK(solo.checkRecorded(0x11, expected) && solo.aligned() && solo.offset() == 0);
		CHECK(!solo.checkRecorded(0x23, expected) && expected == 0x22);
	}
	// constant CRCs: no evidence for an offset, the smallest is taken after MAX_UNDECIDED values
	{
		Engine::ReplayCrcQueue q;
		for (std::uint32_t f = 1; f <= 20; ++f)
			q.addComputed(0x77, f);
		std::uint32_t expected = 0;
		bool ok = true;
		for (std::size_t i = 0; i < Engine::ReplayCrcQueue::MAX_UNDECIDED; ++i)
			ok = ok && q.checkRecorded(0x77, expected);
		CHECK(ok && q.aligned() && q.offset() == 0);
	}
}

static void testScrollTiming()
{
	std::printf("ScrollTiming\n");
	const double tick = 1000000.0 / 30.0;
	// not "near": windef.h defines it as an empty macro
	auto same = [](double a, double b) { return a > b - 1e-9 && a < b + 1e-9; };
	CHECK(same(Engine::scrollTickScale(0), 1.0));									// first tick
	CHECK(Engine::scrollTickScale(33333) > 0.999 && Engine::scrollTickScale(33333) < 1.0);	// nominal tick
	CHECK(same(Engine::scrollTickScale(66667), 66667.0 / tick));					// half the tick rate: twice the step
	CHECK(same(Engine::scrollTickScale(16667), 16667.0 / tick));					// double speed: half the step
	CHECK(same(Engine::scrollTickScale(1000), 1000.0 / tick));					// no lower limit
	CHECK(same(Engine::scrollTickScale(200000), 200000.0 / tick));				// 5 frames per second: full length
	CHECK(same(Engine::scrollTickScale(250000), 250000.0 / tick));				// longest gap still counted
	CHECK(same(Engine::scrollTickScale(300000), 1.0));								// pause or loading: no jump
	// the same real time gives the same distance at 15, 30 and 60 ticks per second
	double slow = 0, normal = 0, fast = 0;
	for (int i = 0; i < 15; ++i) slow += Engine::scrollTickScale(66667);
	for (int i = 0; i < 30; ++i) normal += Engine::scrollTickScale(33333);
	for (int i = 0; i < 60; ++i) fast += Engine::scrollTickScale(16667);
	CHECK(slow > 29.9 && slow < 30.1 && normal > 29.9 && normal < 30.1 && fast > 29.9 && fast < 30.1);

	// Slow frame followed by back-to-back ticks: the sum matches the real time (100 ms = 3 ticks).
	CHECK(same(Engine::scrollTickScale(99000) + Engine::scrollTickScale(500) + Engine::scrollTickScale(500), 100000.0 / tick));
	// 10 frames per second with a capped render-only frame: about 118 ms gap, then three quick ticks.
	CHECK(same(Engine::scrollTickScale(118000) + Engine::scrollTickScale(3000) + Engine::scrollTickScale(12000)
		+ Engine::scrollTickScale(17000), 150000.0 / tick));

	// Speed curve: 0 stands still, the middle is a quarter, the top 1; monotonic.
	CHECK(same(Engine::scrollSpeedCurve(0.0), 0.0));
	CHECK(same(Engine::scrollSpeedCurve(0.5), 0.25));
	CHECK(same(Engine::scrollSpeedCurve(1.0), 1.0));
	CHECK(same(Engine::scrollSpeedCurve(0.1), 0.102 / 3.0));
	CHECK(same(Engine::scrollSpeedCurve(-1.0), 0.0) && same(Engine::scrollSpeedCurve(1.45), 1.0));	// limited
	for (int i = 0; i < 100; ++i)
		CHECK(Engine::scrollSpeedCurve((i + 1) / 100.0) > Engine::scrollSpeedCurve(i / 100.0));

	// Steps in screen units with the stock GameData.ini factors (horizontal 1.6, vertical 2.0, amount 100).
	// Key and edge at the middle: half the former linear value (80 / 100), at the top the former maximum.
	CHECK(same(Engine::scrollKeyStep(1.6, 100, 0.5), 40.0));
	CHECK(same(Engine::scrollKeyStep(2.0, 100, 0.5), 50.0));
	CHECK(same(Engine::scrollKeyStep(1.6, 100, 1.0), 160.0));
	CHECK(same(Engine::scrollKeyStep(1.6, 100, 0.0), 0.0));
	// Drag at the middle: half the plain distance term (1.6 per pixel), plus the minimum 1.6 * 0.25^2.
	CHECK(same(Engine::scrollDragStep(1.6, 100, 1.0, 0.5), 80.0 + 0.1));
	CHECK(same(Engine::scrollDragStep(2.0, -100, -1.0, 0.5), -100.0 - 0.125));
	CHECK(same(Engine::scrollDragStep(1.6, 100, 1.0, 1.0), 320.0 + 1.6));	// top: twice the plain term
	CHECK(same(Engine::scrollDragStep(1.6, 0, 0.0, 0.5), 0.0));				// no distance, no direction: no step
	CHECK(same(Engine::scrollDragStep(1.6, 300, 1.0, 0.0), 0.0));			// setting 0 stands still
	// Ratio drag/edge stays the same at every slider position (one curve for all kinds).
	CHECK(same(Engine::scrollDragStep(1.6, 100, 0.0, 0.2) / Engine::scrollKeyStep(1.6, 100, 0.2),
		Engine::scrollDragStep(1.6, 100, 0.0, 0.8) / Engine::scrollKeyStep(1.6, 100, 0.8)));
}

static void testKeyLayout()
{
	std::printf("KeyLayout\n");
	using Engine::isLayoutLabelKey;
	using Engine::isUsableLayoutChar;
	// typing block: digits, letters, punctuation, the ISO key
	CHECK(isLayoutLabelKey(KEY_1) && isLayoutLabelKey(KEY_0) && isLayoutLabelKey(KEY_EQUAL));
	CHECK(isLayoutLabelKey(KEY_Q) && isLayoutLabelKey(KEY_A) && isLayoutLabelKey(KEY_Z) && isLayoutLabelKey(KEY_M));
	CHECK(isLayoutLabelKey(KEY_TICK) && isLayoutLabelKey(KEY_BACKSLASH) && isLayoutLabelKey(KEY_SLASH));
	CHECK(isLayoutLabelKey(KEY_102));
	// keys that keep their table entries
	CHECK(!isLayoutLabelKey(KEY_ESC) && !isLayoutLabelKey(KEY_BACKSPACE) && !isLayoutLabelKey(KEY_TAB));
	CHECK(!isLayoutLabelKey(KEY_ENTER) && !isLayoutLabelKey(KEY_LSHIFT) && !isLayoutLabelKey(KEY_SPACE));
	CHECK(!isLayoutLabelKey(KEY_F1) && !isLayoutLabelKey(KEY_KP7) && !isLayoutLabelKey(KEY_KPSLASH));
	// Latin letters, digits, punctuation, umlauts and currency signs label keys
	CHECK(isUsableLayoutChar(u'a') && isUsableLayoutChar(u'Z') && isUsableLayoutChar(u'7') && isUsableLayoutChar(u'#'));
	CHECK(isUsableLayoutChar(0x00DF) && isUsableLayoutChar(0x00FC) && isUsableLayoutChar(0x00E9));	// sharp s, u umlaut, e acute
	CHECK(isUsableLayoutChar(0x20AC) && isUsableLayoutChar(0x00A7) && isUsableLayoutChar(0x0151));	// euro, section, o double acute
	// other scripts, control characters and nothing do not
	CHECK(!isUsableLayoutChar(0x0444) && !isUsableLayoutChar(0x03B1) && !isUsableLayoutChar(0x3042));	// Cyrillic ef, alpha, hiragana a
	CHECK(!isUsableLayoutChar(0) && !isUsableLayoutChar(u'\b') && !isUsableLayoutChar(0x7F));
}

static void testInputQueue()
{
	std::printf("InputQueue\n");
	using Engine::replacesPreviousMouseEvent;
	const unsigned int MOVE = 0x0200, LEFT_DOWN = 0x0201, LEFT_UP = 0x0202, WHEEL = 0x020A;
	const std::uint64_t NONE = 0, LEFT = 0x0001, SHIFT = 0x0004;
	CHECK(replacesPreviousMouseEvent(MOVE, NONE, MOVE, NONE));					// plain moves merge
	CHECK(replacesPreviousMouseEvent(MOVE, LEFT, MOVE, LEFT));					// dragging moves merge
	CHECK(!replacesPreviousMouseEvent(MOVE, NONE, MOVE, LEFT));				// button pressed in between
	CHECK(!replacesPreviousMouseEvent(MOVE, NONE, MOVE, SHIFT));				// modifier changed
	CHECK(!replacesPreviousMouseEvent(LEFT_DOWN, LEFT, MOVE, LEFT));			// a press keeps its position
	CHECK(!replacesPreviousMouseEvent(MOVE, LEFT, LEFT_UP, NONE));			// a release is never merged
	CHECK(!replacesPreviousMouseEvent(MOVE, NONE, WHEEL, NONE));
	CHECK(!replacesPreviousMouseEvent(WHEEL, NONE, WHEEL, NONE));				// wheel steps add up elsewhere
	CHECK(Engine::inputAgeMs(1000, 1016) == 16);
	CHECK(Engine::inputAgeMs(0xFFFFFFF0u, 0x10) == 0x20);						// across the wrap of the clock
}

// ------------------------------------------------------------------ preference files

static void testPreferenceFile()
{
	using namespace Engine;
	std::printf("PreferenceFile\n");
	std::string key, value;
	CHECK(parsePreferenceLine("Gamma = 50\n", key, value) && key == "Gamma" && value == "50");
	CHECK(parsePreferenceLine("\tResolution=1920 1080\r\n", key, value) && key == "Resolution" && value == "1920 1080");
	CHECK(parsePreferenceLine("IPAddress = a==b", key, value) && value == "a==b");	// only the first '=' splits
	CHECK(!parsePreferenceLine("[Graphics]\n", key, value));		// no '=': skipped, never a key
	CHECK(!parsePreferenceLine("Section", key, value));
	CHECK(!parsePreferenceLine("\n", key, value));
	CHECK(!parsePreferenceLine("", key, value));
	CHECK(!parsePreferenceLine("Key =   \n", key, value));		// empty value
	CHECK(!parsePreferenceLine(" = 5", key, value));				// empty key

	// two readers of one file: the options menu (A) loaded it before B changed and wrote it
	const PreferenceValues loadedA = {{"Gamma", "50"}, {"MusicVolume", "70"}, {"CampaignDifficulty", "0"}};
	PreferenceValues currentA = loadedA;
	currentA["Gamma"] = "60";					// A's own change
	currentA["IPAddress"] = "";					// left behind by a lookup through operator[]
	PreferenceValues onDisk = {{"Gamma", "50"}, {"MusicVolume", "70"}, {"CampaignDifficulty", "2"}, {"IPAddress", "10.0.0.2"}};
	mergePreferenceChanges(loadedA, currentA, onDisk);
	CHECK(onDisk.at("Gamma") == "60");				// A's change
	CHECK(onDisk.at("CampaignDifficulty") == "2");	// B's change survives
	CHECK(onDisk.at("IPAddress") == "10.0.0.2");	// the empty lookup does not erase it
	CHECK(onDisk.at("MusicVolume") == "70");

	// a value cleared or removed by the reader goes away; a new one is added
	PreferenceValues current = loadedA;
	current["MusicVolume"] = "";
	current.erase("CampaignDifficulty");
	current["SFXVolume"] = "40";
	onDisk = loadedA;
	onDisk["Language"] = "german";
	mergePreferenceChanges(loadedA, current, onDisk);
	CHECK(onDisk.find("MusicVolume") == onDisk.end());
	CHECK(onDisk.find("CampaignDifficulty") == onDisk.end());
	CHECK(onDisk.at("SFXVolume") == "40" && onDisk.at("Language") == "german" && onDisk.at("Gamma") == "50");

	// nothing changed: the file stays as it is, including what others wrote
	onDisk = {{"Gamma", "55"}};
	mergePreferenceChanges(loadedA, loadedA, onDisk);
	CHECK((onDisk == PreferenceValues{{"Gamma", "55"}}));
}

// ------------------------------------------------------------------ settings

static void testSettingsSchema()
{
	using namespace Engine::Settings;
	std::printf("Settings schema\n");
	std::set<std::string> ids;
	std::set<std::string> legacy;
	bool wellFormed = true;
	for (const Definition &def : schema())
	{
		const std::string id = def.id;
		std::string lowerId = id;
		for (char &c : lowerId)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		CHECK_SILENT(ids.insert(lowerId).second);				// no id twice
		const std::size_t dot = id.find('.');
		wellFormed = wellFormed && dot != std::string::npos && dot > 0 && dot + 1 < id.size();
		CHECK_SILENT(def.description != nullptr && def.description[0] != 0);
		CHECK_SILENT(def.type != Type::Enum || def.choices != nullptr);
		if (def.defaultValue != nullptr)
		{
			std::string normalized;
			CHECK_SILENT(normalizeValue(def, def.defaultValue, normalized));	// defaults are valid
		}
		if (def.legacyKey != nullptr)
			CHECK_SILENT(legacy.insert(std::to_string(static_cast<int>(def.legacyFile)) + def.legacyKey).second);
		for (const char *dropped : droppedLegacyKeys())
			CHECK_SILENT(def.legacyKey == nullptr || std::string(dropped) != def.legacyKey);	// dropped keys have no reader
	}
	CHECK(wellFormed && ids.size() == schema().size() && schema().size() > 50);
	CHECK(findDefinition("display.GAMMA") != nullptr && std::string(findDefinition("Display.Gamma")->id) == "Display.Gamma");
	CHECK(findDefinition("Display.Nothing") == nullptr);
	CHECK(findDefinition("Detail.TextureQuality")->commandLine != nullptr);
	CHECK(findDefinition("Lan.UserName")->file == File::State);
}

static void testSettingsValues()
{
	using namespace Engine::Settings;
	std::printf("Settings values\n");
	std::string out;
	bool clamped = false;
	const Definition &scroll = *findDefinition("Controls.ScrollFactor");	// 0..100 like its old reader
	CHECK(normalizeValue(scroll, " 70 ", out, &clamped) && out == "70" && !clamped);
	CHECK(normalizeValue(scroll, "250", out, &clamped) && out == "100" && clamped);
	CHECK(normalizeValue(scroll, "-5", out, &clamped) && out == "0" && clamped);
	CHECK(!normalizeValue(scroll, "hell", out));
	CHECK(!normalizeValue(scroll, "50abc", out));
	CHECK(!normalizeValue(scroll, "", out));
	// AntiAliasing: only the values the old reader knows; anything else means off there and is not
	// taken over (clamping 17 to 16 would switch on 16x multisampling)
	const Definition &aa = *findDefinition("Display.AntiAliasing");
	CHECK(normalizeValue(aa, "8", out) && out == "8");
	CHECK(normalizeValue(aa, "1", out) && out == "1");
	CHECK(!normalizeValue(aa, "17", out) && !normalizeValue(aa, "3", out));
	// no ranges where the old reader has none
	CHECK(normalizeValue(*findDefinition("Display.Gamma"), "140", out, &clamped) && out == "140" && !clamped);
	CHECK(normalizeValue(*findDefinition("Detail.MaxParticleCount"), "200000", out, &clamped) && out == "200000" && !clamped);
	CHECK(normalizeValue(*findDefinition("Audio.MusicVolume"), "70.5", out) && out == "70.5");
	const Definition &vsync = *findDefinition("Display.VSync");
	CHECK(normalizeValue(vsync, "TRUE", out) && out == "yes");
	CHECK(normalizeValue(vsync, "Off", out) && out == "no");
	CHECK(normalizeValue(vsync, "0", out) && out == "no");
	CHECK(!normalizeValue(vsync, "maybe", out));
	// Direct3D 11 effects: one key per effect, "auto" (the detail level decides) by default, exceptions as choices
	int effectKeys = 0;
	for (const Definition &definition : schema())
	{
		if (std::string_view(definition.id).rfind("Effects.", 0) != 0)
			continue;
		++effectKeys;
		CHECK(definition.type == Type::Enum && definition.defaultValue != nullptr && std::string(definition.defaultValue) == "auto");
		CHECK(definition.choices != nullptr && std::string_view(definition.choices).rfind("auto|off|", 0) == 0);
		CHECK(definition.file == File::Settings && definition.commandLine == nullptr);
	}
	CHECK(effectKeys == 27);
	CHECK(normalizeValue(*findDefinition("Effects.Bloom"), "2", out) && out == "2");
	CHECK(!normalizeValue(*findDefinition("Effects.Bloom"), "3", out));
	CHECK(normalizeValue(*findDefinition("Effects.ShadowMap"), "4096", out) && out == "4096");
	CHECK(normalizeValue(*findDefinition("Effects.Sharpen"), "0.25", out) && out == "0.25");
	CHECK(findDefinition("Effects.TerrainTess")->apply == Apply::NextMatch && findDefinition("Effects.Bloom")->apply == Apply::Immediate);
	const Definition &tier = *findDefinition("Detail.TextureQuality");
	CHECK(normalizeValue(tier, "uhd", out) && out == "UHD");
	CHECK(!normalizeValue(tier, "4K", out));
	const Definition &resolution = *findDefinition("Display.Resolution");
	CHECK(normalizeValue(resolution, "2560 1440", out) && out == "2560 1440");
	CHECK(normalizeValue(resolution, "1920x1080", out) && out == "1920 1080");
	CHECK(!normalizeValue(resolution, "1920", out));
	CHECK(!normalizeValue(resolution, "0 10", out));
	const Definition &list = *findDefinition("Lan.RemoteIPs");
	CHECK(normalizeValue(list, " 10.0.0.2 ;; host:name ; ", out) && out == "10.0.0.2;host:name");
	bool flag = false;
	CHECK(parseBool("Yes", flag) && flag && parseBool("no", flag) && !flag && !parseBool("", flag));
}

static void testSettingsDocument()
{
	using namespace Engine::Settings;
	std::printf("Settings document\n");
	const std::string text =
		"\xEF\xBB\xBF; my comment\r\n"
		"[Display]\r\n"
		"Gamma = 60\r\n"
		"\r\n"
		"MyOwnKey = keep me\r\n"
		"garbage line\r\n"
		"\r\n"
		"[audio]\r\n"
		"MusicVolume=40\r\n";
	Document doc;
	std::vector<Document::Problem> problems;
	doc.parse(text, &problems);
	CHECK(problems.size() == 1 && problems[0].line == 6 && problems[0].text == "garbage line");
	CHECK(doc.find("display", "GAMMA") != nullptr && *doc.find("display", "GAMMA") == "60");
	CHECK(doc.find("Audio", "MusicVolume") != nullptr && *doc.find("Audio", "MusicVolume") == "40");
	CHECK(doc.find("Audio", "Gamma") == nullptr);

	doc.set("Display", "Gamma", "70");						// changed in place
	doc.set("Display", "VSync", "no", "Wait for the display refresh");	// added to its section
	doc.set("Controls", "ScrollFactor", "30");				// new section at the end
	const std::string expected =
		"; my comment\r\n"
		"[Display]\r\n"
		"Gamma = 70\r\n"
		"\r\n"
		"MyOwnKey = keep me\r\n"
		"garbage line\r\n"
		"; Wait for the display refresh\r\n"
		"VSync = no\r\n"
		"\r\n"
		"[audio]\r\n"
		"MusicVolume=40\r\n"
		"\r\n"
		"[Controls]\r\n"
		"ScrollFactor = 30\r\n";
	CHECK(doc.text() == expected);
	if (doc.text() != expected)
		std::printf("%s", doc.text().c_str());

	Document lf;
	lf.parse("[A]\nx = 1\n");
	lf.set("A", "y", "2");
	CHECK(lf.text() == "[A]\nx = 1\ny = 2\n");			// keeps LF line endings
	Document fresh;
	fresh.set("A", "x", "1");
	CHECK(fresh.text() == "[A]\r\nx = 1\r\n");				// new files use CRLF

	Document twice;
	twice.parse("[A]\nx = 1\nx = 2\n");
	CHECK(*twice.find("A", "x") == "2");					// the last one counts
	twice.set("A", "x", "3");
	CHECK(twice.text() == "[A]\nx = 1\nx = 3\n");
	CHECK(twice.entries().size() == 2 && twice.entries()[1][2] == "3");
	Document own;
	own.parse("[A]\n; mine\nx = 1\n; Described\ny = 2\nz = 3\n");
	own.remove("A", "y", "Described");						// its own comment goes too
	own.remove("A", "x", "Described");						// the user's comment stays
	CHECK(own.text() == "[A]\n; mine\nz = 3\n");
}

static void testSettingsStore()
{
	using namespace Engine::Settings;
	std::printf("Settings store\n");
	Store store;
	CHECK(store.text("Display.Gamma") == "50" && store.sourceOf("Display.Gamma") == Source::Default);
	CHECK(store.sourceOf("Audio.MusicVolume") == Source::None && store.text("Audio.MusicVolume").empty());
	store.setDefault("Audio.MusicVolume", "80");
	CHECK(store.getInt("Audio.MusicVolume") == 80 && store.sourceOf("Audio.MusicVolume") == Source::Default);

	Report report;
	store.loadFile(File::Settings,
		"[Display]\nVSync = maybe\n[Controls]\nScrollFactor = 140\n[Audio]\nMusicVolume = 30\n[Mod]\nSpecial = 1\n", &report);
	CHECK(store.getInt("Controls.ScrollFactor") == 100 && store.sourceOf("Controls.ScrollFactor") == Source::File);
	CHECK(store.getBool("Display.VSync") && store.sourceOf("Display.VSync") == Source::Default);	// invalid: ignored
	CHECK(store.getInt("Audio.MusicVolume") == 30);
	CHECK(report.lines.size() == 3);		// clamped, invalid, unknown kept
	CHECK(!store.dirty(File::Settings));

	int calls = 0;
	{
		std::shared_ptr<void> watch = store.observe("audio.musicvolume", [&calls]() { ++calls; });
		CHECK(store.setInt("Audio.MusicVolume", 45) && calls == 1);
		CHECK(store.setInt("Audio.MusicVolume", 45) && calls == 1);		// same value: no call
		CHECK(!store.set("Audio.MusicVolume", "loud") && calls == 1);
		CHECK(!store.set("Audio.Nothing", "1"));
	}
	CHECK(store.setInt("Audio.MusicVolume", 46) && calls == 1);			// observer released

	// command line wins and is never written
	CHECK(store.setCommandLine("Display.Gamma", "50"));
	CHECK(store.getInt("Display.Gamma") == 50 && store.sourceOf("Display.Gamma") == Source::CommandLine);
	CHECK(store.setInt("Display.Gamma", 65));
	CHECK(store.getInt("Display.Gamma") == 50);
	CHECK(!store.setCommandLine("Display.Gamma", "x"));
	CHECK(store.dirty(File::Settings) && !store.dirty(File::State));
	const std::string saved = store.fileText(File::Settings);
	CHECK(saved.find("Gamma = 65") != std::string::npos);
	CHECK(saved.find("Special = 1") != std::string::npos);					// unknown key kept
	CHECK(saved.find("MusicVolume = 46") != std::string::npos);
	store.markSaved(File::Settings);
	CHECK(!store.dirty(File::Settings));

	// a new file gets a header comment and a comment per key
	Store empty;
	CHECK(empty.setBool("Hints.HardwareHintShown", true));
	const std::string state = empty.fileText(File::State);
	CHECK(state.rfind("; GZH Neo state", 0) == 0 && state.find("[Hints]") != std::string::npos
		&& state.find("HardwareHintShown = yes") != std::string::npos);
	CHECK(empty.fileText(File::Settings).empty());

	// the mod list as the options menu writes it, and as the next start reads it
	Store mods;
	mods.loadFile(File::Settings, "[Mods]\nActive = Old\n");
	CHECK(mods.text("Mods.Active") == "Old");
	CHECK(mods.set("Mods.Active", "Base;Units.zip"));
	const std::string written = mods.fileText(File::Settings);
	CHECK(written.find("Active = Base;Units.zip") != std::string::npos && written.find("Old") == std::string::npos);
	Store restarted;
	restarted.loadFile(File::Settings, written);
	const std::vector<std::string> list = Engine::splitModList(restarted.text("Mods.Active"));
	CHECK(list.size() == 2 && list[0] == "Base" && list[1] == "Units.zip");
	restarted.unset("Mods.Active");
	CHECK(restarted.fileText(File::Settings).find("Active =") == std::string::npos);
}

static void testSettingsImport()
{
	using namespace Engine::Settings;
	std::printf("Settings import\n");
	CHECK(cp1252ToUtf8("M\xFC" "ller \x80") == "M\xC3\xBC" "ller \xE2\x82\xAC");
	CHECK(decodeQuotedAscii("maps_5Cmy_5Fmap_2Emap") == "maps\\my_map.map");
	CHECK(decodeQuotedUnicode("A_00_FC_00") == "A\xC3\xBC");		// "Aü" as UTF-16 bytes
	CHECK(decodeQuotedUnicode("_3D_D8_00_DE") == "\xF0\x9F\x98\x80");	// a surrogate pair

	LegacyFiles files;
	files.options =
		"Gamma = 70\n"
		"Resolution = 1920 1080\n"
		"StaticGameLOD = veryhigh\n"
		"UseShadowVolumes = Yes\n"
		"TextureFilter = 2\n"			// no reader any more
		"SomethingElse = 1\n"			// unknown
		"ScrollFactor = 250\n"			// out of range
		"Language = german\n"
		"HardwareHintShown = yes\n"
		"[Section]\n"					// line without '='
		"SaveCameraInReplays = no\n"
		"AntiAliasing = 17\n";			// means off for the old reader: not taken over
	files.network =
		"UserName = A_00_FC_00\n"
		"Map = maps_5Cmy_5Fmap_5Cmy_5Fmap_2Emap\n"
		"NumRemoteIPs = 2\n"
		"RemoteIP0 = 10.0.0.2:Bob\n"
		"RemoteIP1 = host.lan\n"
		"Color = 3\n";
	files.skirmish =
		"Map = maps\\tournament_desert\\tournament_desert.map\n"	// value with raw backslashes
		"FPS = 30\n"
		"SlotList = H,0,-1,-1,-1:X:X:X:X:X:X:X:\n";

	Store store;
	Report report;
	importLegacy(store, files, report);
	CHECK(store.getInt("Display.Gamma") == 70);
	CHECK(store.text("Display.Resolution") == "1920 1080");
	CHECK(store.text("Detail.StaticGameLOD") == "VeryHigh");
	CHECK(store.getBool("Detail.UseShadowVolumes"));
	CHECK(store.getInt("Controls.ScrollFactor") == 100);
	CHECK(store.text("General.Language") == "german");
	CHECK(!store.getBool("Replays.SaveCamera"));
	CHECK(store.getBool("Hints.HardwareHintShown"));
	CHECK(store.text("Lan.UserName") == "A\xC3\xBC");
	CHECK(store.text("Lan.Map") == "maps\\my_map\\my_map.map");
	CHECK(store.text("Lan.RemoteIPs") == "10.0.0.2:Bob;host.lan");
	CHECK(store.getInt("Lan.Color") == 3);
	CHECK(store.text("Skirmish.Map") == "maps\\tournament_desert\\tournament_desert.map");
	CHECK(store.getInt("Skirmish.FPS") == 30);
	CHECK(store.text("Skirmish.SlotList") == "H,0,-1,-1,-1:X:X:X:X:X:X:X:");
	CHECK(store.dirty(File::Settings) && store.dirty(File::State));

	auto has = [&report](const char *part)
	{
		return std::any_of(report.lines.begin(), report.lines.end(),
			[part](const std::string &line) { return line.find(part) != std::string::npos; });
	};
	CHECK(has("Options.ini TextureFilter: no reader any more"));
	CHECK(has("Options.ini SomethingElse: unknown"));
	CHECK(has("ScrollFactor -> Settings.ini Controls.ScrollFactor = 100 (out of range, limited)"));
	CHECK(!has("Section"));
	CHECK(has("Options.ini AntiAliasing = 17: invalid, not imported") && store.fileValue("Display.AntiAliasing") == nullptr);
	CHECK(!has("NumRemoteIPs"));
	CHECK(!has("RemoteIP0:"));

	// the imported files read back to the same values
	Store again;
	again.loadFile(File::Settings, store.fileText(File::Settings));
	again.loadFile(File::State, store.fileText(File::State));
	CHECK(again.text("Lan.UserName") == "A\xC3\xBC" && again.getInt("Display.Gamma") == 70
		&& again.text("Skirmish.Map") == store.text("Skirmish.Map"));

	// nothing to import: nothing written
	Store none;
	Report quiet;
	importLegacy(none, LegacyFiles(), quiet);
	CHECK(quiet.lines.empty() && !none.dirty(File::Settings) && !none.dirty(File::State));
}

static void testSettingsTextConversions()
{
	using namespace Engine::Settings;
	std::printf("Settings text conversions\n");
	// conversions both ways
	CHECK(utf8ToCp1252("M\xC3\xBCller \xE2\x82\xAC \xE4\xB8\xAD") == "M\xFCller \x80 ?");
	CHECK(utf8ToCp1252("bad \xC3") == "bad ?");
	CHECK(encodeQuotedAscii("maps\\my_map.map") == "maps_5Cmy_5Fmap_2Emap");
	CHECK(decodeQuotedAscii(encodeQuotedAscii("Map \xFC\x80")) == "Map \xFC\x80");
	CHECK(encodeQuotedUnicode("A\xC3\xBC") == "A_00_FC_00");
	CHECK(decodeQuotedUnicode(encodeQuotedUnicode("\xF0\x9F\x98\x80 x")) == "\xF0\x9F\x98\x80 x");
}

// ------------------------------------------------------------------ virtual file system

// A .big archive with real contents: name -> bytes, stored in the given order.
static std::vector<unsigned char> makeBigWithData(const std::vector<std::pair<std::string, std::string>> &files)
{
	std::size_t header = 16;
	for (const auto &file : files)
		header += 8 + file.first.size() + 1;
	std::vector<Engine::BigArchiveEntry> entries;
	std::uint32_t offset = static_cast<std::uint32_t>(header);
	std::string data;
	for (const auto &file : files)
	{
		entries.push_back({file.first, offset, static_cast<std::uint32_t>(file.second.size())});
		offset += static_cast<std::uint32_t>(file.second.size());
		data += file.second;
	}
	std::vector<unsigned char> archive = makeBigArchive(entries, 0);
	archive.insert(archive.end(), data.begin(), data.end());
	return archive;
}

static Engine::Vfs::ReadAt readAtMemory(std::shared_ptr<const std::vector<unsigned char>> bytes)
{
	return [bytes](std::uint64_t offset, void *dest, std::size_t size) -> std::size_t
	{
		if (offset >= bytes->size())
			return 0;
		const std::size_t n = std::min<std::size_t>(size, bytes->size() - static_cast<std::size_t>(offset));
		std::memcpy(dest, bytes->data() + offset, n);
		return n;
	};
}

static std::string readAll(const Engine::Vfs::Source &source, const Engine::Vfs::Entry &entry)
{
	std::string text(static_cast<std::size_t>(entry.size), '\0');
	const std::size_t got = source.read(entry, 0, text.data(), text.size());
	text.resize(got);
	return text;
}

// A source that only lists names (contents are not read): for plan and index tests.
static std::unique_ptr<Engine::Vfs::FolderSource> makeListSource(const std::string &name, const std::vector<std::string> &paths)
{
	std::vector<Engine::Vfs::FolderSource::File> files;
	for (const std::string &path : paths)
		files.push_back({path, 0});
	return std::make_unique<Engine::Vfs::FolderSource>(name, files,
		[](const std::string &, std::uint64_t, void *, std::size_t) -> std::size_t { return 0; });
}

static void testVfsPath()
{
	using namespace Engine::Vfs;
	std::printf("Vfs paths\n");
	std::string key;
	CHECK(normalizePath("Data\\INI\\Object\\A.INI", key) && key == "data/ini/object/a.ini");
	CHECK(normalizePath("./Art//Textures/x.DDS", key) && key == "art/textures/x.dds");
	CHECK(normalizePath("\\Leading\\x.ini\\", key) && key == "leading/x.ini");
	CHECK(normalizePath("Maps\\Readme", key) && key == "maps/readme");	// no extension: still a file
	CHECK(normalizePath("Mod.v2\\a.ini", key) && key == "mod.v2/a.ini");	// dot in a folder name
	CHECK(normalizePath("\xC4pfel\\x.ini", key) && key == "\xC4pfel/x.ini");	// only ASCII is lowered
	CHECK(!normalizePath("..\\x.ini", key) && key.empty());
	CHECK(!normalizePath("a/../b.ini", key));
	CHECK(!normalizePath("C:\\x.ini", key));
	CHECK(!normalizePath("", key));
	CHECK(!normalizePath("/\\./", key));

	// legacy order: '/' sorts like '\\', which comes after the digits and before '_' and letters
	CHECK(compareLegacyOrder("a/x.ini", "a1.ini") > 0);
	CHECK(compareLegacyOrder("a\\x.ini", "a1.ini") > 0);
	CHECK(compareLegacyOrder("a/b", "a_b") < 0);
	CHECK(compareLegacyOrder("A.ini", "a.INI") == 0);
	CHECK(compareLegacyOrder("a.ini", "a.ini2") < 0);
	CHECK(compareLegacyOrder("Data\\INI\\INIZH.big", "INIZH.big") < 0);
	CHECK(compareLegacyOrder("INIZH.big", "ZH_Generals\\INI.big") < 0);

	CHECK(wildcardMatch("object.ini", "*.ini"));
	CHECK(wildcardMatch("OBJECT.INI", "*.ini"));
	CHECK(!wildcardMatch("object.inix", "*.ini"));
	CHECK(wildcardMatch("abc", "a?c"));
	CHECK(wildcardMatch("aXbXc", "a*b*c"));
	CHECK(!wildcardMatch("ab", "a*b*c"));
	CHECK(wildcardMatch("", "*"));
	CHECK(!wildcardMatch("x", ""));
	CHECK(wildcardMatch("a.b.c", "*.c"));

	CHECK(isTexturePackagePath("art/textures/a.dds"));
	CHECK(isTexturePackagePath("art/terrain/b.tga"));
	CHECK(isTexturePackagePath("data/german/art/textures/c.dds"));
	CHECK(isTexturePackagePath("texturpaket.txt"));
	CHECK(!isTexturePackagePath("art/textures/texturpaket.txt"));
	CHECK(!isTexturePackagePath("art/w3d/a.w3d"));
	CHECK(!isTexturePackagePath("art/textures/a.ini"));
	CHECK(!isTexturePackagePath("data/ini/a.dds"));
}

static void testVfsSources()
{
	using namespace Engine::Vfs;
	std::printf("Vfs sources\n");

	auto bytes = std::make_shared<const std::vector<unsigned char>>(makeBigWithData({
		{"Data\\INI\\A.ini", "hello"},
		{"art\\textures\\t.dds", "world!!"},
		{"..\\evil.ini", "x"},
		{"data/ini/a.ini", "again"}}));	// same key as the first entry: the later one counts
	Engine::BigArchiveError error = Engine::BigArchiveError::NotBig;
	std::unique_ptr<BigSource> big = BigSource::open("INIZH.big", readAtMemory(bytes), bytes->size(), &error);
	CHECK(big != nullptr && error == Engine::BigArchiveError::None);
	if (big)
	{
		CHECK(big->name() == "INIZH.big");
		CHECK(big->entries().size() == 2);
		CHECK(big->skippedEntries() == 1);
		const Entry &a = big->entries()[0];
		CHECK(a.key == "data/ini/a.ini" && a.stored == "data/ini/a.ini" && readAll(*big, a) == "again");
		const Entry &t = big->entries()[1];
		CHECK(t.key == "art/textures/t.dds" && readAll(*big, t) == "world!!");
		char part[4] = {};
		CHECK(big->read(t, 2, part, 3) == 3 && std::memcmp(part, "rld", 3) == 0);
		CHECK(big->read(t, 5, part, 4) == 2);		// only up to the end of the entry
		CHECK(big->read(t, 7, part, 4) == 0);
	}

	std::vector<unsigned char> damaged = *bytes;
	damaged[0] = 'X';
	CHECK(BigSource::open("bad.big", readAtMemory(std::make_shared<const std::vector<unsigned char>>(damaged)),
		damaged.size(), &error) == nullptr && error == Engine::BigArchiveError::NotBig);

	const std::map<std::string, std::string> disk = {{"Data\\INI\\Mod.ini", "modded"}, {"Readme", "text"}};
	std::vector<FolderSource::File> files;
	for (const auto &file : disk)
		files.push_back({file.first, file.second.size()});
	FolderSource folder("Mods\\Test\\", files,
		[&disk](const std::string &path, std::uint64_t offset, void *dest, std::size_t size) -> std::size_t
		{
			const std::string &text = disk.at(path);
			if (offset >= text.size())
				return 0;
			const std::size_t n = std::min<std::size_t>(size, text.size() - static_cast<std::size_t>(offset));
			std::memcpy(dest, text.data() + offset, n);
			return n;
		});
	CHECK(folder.entries().size() == 2);
	CHECK(folder.entries()[0].key == "data/ini/mod.ini" && folder.entries()[0].stored == "Data\\INI\\Mod.ini");
	CHECK(readAll(folder, folder.entries()[0]) == "modded");
	CHECK(folder.entries()[1].key == "readme" && readAll(folder, folder.entries()[1]) == "text");
}

static void testVfsMountPlan()
{
	using namespace Engine::Vfs;
	std::printf("Vfs mount plan\n");

	MountInputs in;
	in.baseArchives = {"INIZH.big", "Data\\INI\\INIZH.big", "ZH_Generals\\INI.big", "BaseZH.big"};
	in.baseGameArchives = {"G:\\Gen\\INI.big"};
	in.tier = 2;
	in.packageArchives = {"Texturstufen\\Paket\\TexturesPaket2.big", "Texturstufen\\Paket\\TexturesPaket.big"};
	ModInput mod;
	mod.folder = "M:\\Mod\\";
	mod.folderArchives = {"M:\\Mod\\x.big", "M:\\Mod\\y.zip"};
	mod.packageArchives = {"M:\\Mod\\Texturstufen\\Paket\\t.big"};
	mod.hasLooseFiles = true;
	in.mods = {mod};

	std::vector<Mount> plan = buildMountPlan(in);
	const std::vector<std::string> expected = {
		"M:\\Mod\\Texturstufen\\Paket\\t.big", "M:\\Mod\\", "M:\\Mod\\y.zip", "M:\\Mod\\x.big",
		"Texturstufen\\Paket\\TexturesPaket2.big", "Texturstufen\\Paket\\TexturesPaket.big",
		"BaseZH.big", "Data\\INI\\INIZH.big", "INIZH.big", "ZH_Generals\\INI.big",
		"G:\\Gen\\INI.big"};
	CHECK(plan.size() == expected.size());
	bool same = plan.size() == expected.size();
	for (std::size_t i = 0; same && i < plan.size(); ++i)
		same = plan[i].path == expected[i];
	CHECK(same);
	if (plan.size() == expected.size())
	{
		CHECK(plan[0].rank == Rank::ModPackage && plan[0].texturesOnly && plan[0].mod == 0);
		CHECK(plan[1].rank == Rank::ModLoose && plan[1].folder);
		CHECK(plan[2].rank == Rank::ModArchives && !plan[2].texturesOnly);
		CHECK(plan[4].rank == Rank::GamePackage && plan[4].texturesOnly);
		CHECK(plan[6].rank == Rank::BaseArchives && plan[10].rank == Rank::BaseGame);
	}

	// HD loads the same packages as UHD (the tier only sets the mip offset); SD none
	in.tier = 1;
	CHECK(buildMountPlan(in).size() == expected.size());
	in.tier = 0;
	plan = buildMountPlan(in);
	CHECK(std::none_of(plan.begin(), plan.end(), [](const Mount &m) { return m.texturesOnly; }));

	// a folder without loose files mounts only its archives
	in.mods[0].hasLooseFiles = false;
	plan = buildMountPlan(in);
	CHECK(std::none_of(plan.begin(), plan.end(), [](const Mount &m) { return m.folder; }));

	// the old engine kept one folder mod and one archive mod, the folder above the archive,
	// whatever the order on the command line
	ModInput folderMod;
	folderMod.folder = "D\\";
	folderMod.folderArchives = {"D\\d.big"};
	ModInput archiveMod;
	archiveMod.archive = "m.big";
	for (int order = 0; order < 2; ++order)
	{
		MountInputs two;
		two.mods = order == 0 ? std::vector<ModInput>{folderMod, archiveMod} : std::vector<ModInput>{archiveMod, folderMod};
		plan = buildMountPlan(two);
		CHECK(plan.size() == 2 && plan[0].path == "D\\d.big" && plan[1].path == "m.big");
	}
	// otherwise the last mod on the command line ranks highest
	ModInput second;
	second.archive = "n.big";
	MountInputs three;
	three.mods = {archiveMod, second, folderMod};
	plan = buildMountPlan(three);
	CHECK(plan.size() == 3 && plan[0].path == "D\\d.big" && plan[1].path == "n.big" && plan[2].path == "m.big");
	CHECK(std::strcmp(rankName(Rank::ModLoose), "mod folder") == 0);
}

static void testVfsIndex()
{
	using namespace Engine::Vfs;
	std::printf("Vfs index\n");

	auto tier = makeListSource("Texturstufen\\HD\\t.big", {"Art\\Textures\\t.dds"});
	auto badTier = makeListSource("Texturstufen\\HD\\bad.big", {"art\\textures\\u.dds", "data\\ini\\x.ini"});
	auto base1 = makeListSource("A.big", {"Data\\INI\\a.ini", "data\\ini\\object\\z.ini", "data\\ini\\object1.ini"});
	auto base2 = makeListSource("B.big", {"Data\\INI\\a.ini", "Data\\INI\\b.ini", "art\\textures\\u.dds"});

	Index index;
	std::vector<std::string> rejected;
	index.build({{tier.get(), true}, {badTier.get(), true}, {base1.get(), false}, {base2.get(), false}}, &rejected);
	CHECK(rejected.size() == 1 && rejected[0] == "Texturstufen\\HD\\bad.big");
	CHECK(index.size() == 6);

	const Hit *hit = index.find("DATA\\INI\\A.INI");
	CHECK(hit != nullptr && hit->source == base1.get() && hit->layer == 2 && hit->entry->key == "data/ini/a.ini");
	hit = index.find("data/ini/b.ini");
	CHECK(hit != nullptr && hit->source == base2.get());
	hit = index.find("art/textures/t.dds");
	CHECK(hit != nullptr && hit->source == tier.get());
	hit = index.find("art/textures/u.dds");		// the rejected tier archive does not count
	CHECK(hit != nullptr && hit->source == base2.get());
	CHECK(index.find("data/ini/x.ini") == nullptr);
	CHECK(index.find("..\\data\\ini\\a.ini") == nullptr);

	std::vector<std::string> list = index.list("Data\\INI", "*.ini", false);
	CHECK((list == std::vector<std::string>{"data/ini/a.ini", "data/ini/b.ini", "data/ini/object1.ini"}));
	list = index.list("data/ini/", "*.ini", true);
	CHECK((list == std::vector<std::string>{"data/ini/a.ini", "data/ini/b.ini", "data/ini/object1.ini", "data/ini/object/z.ini"}));
	list = index.list("", "*.dds", true);
	CHECK((list == std::vector<std::string>{"art/textures/t.dds", "art/textures/u.dds"}));
	CHECK(index.list("", "*.dds", false).empty());
	CHECK(index.list("data/in", "*", true).empty());	// a prefix of a folder name is not the folder
	CHECK(index.list("..", "*", true).empty());

	// many threads look up and read at the same time (ThreadSanitizer run of the gate)
	auto bytes = std::make_shared<const std::vector<unsigned char>>(makeBigWithData({{"a.ini", "alpha"}, {"b.ini", "beta"}}));
	std::unique_ptr<BigSource> big = BigSource::open("data.big", readAtMemory(bytes), bytes->size());
	CHECK(big != nullptr);
	if (big)
	{
		Index shared;
		shared.build({{big.get(), false}});
		std::atomic<int> bad{0};
		std::vector<std::thread> threads;
		for (int t = 0; t < 4; ++t)
			threads.emplace_back([&shared, &bad, t]()
			{
				for (int i = 0; i < 2000; ++i)
				{
					const bool first = ((i + t) & 1) == 0;
					const Hit *h = shared.find(first ? "A.INI" : "b.ini");
					if (h == nullptr || readAll(*h->source, *h->entry) != (first ? "alpha" : "beta"))
						++bad;
				}
			});
		for (std::thread &thread : threads)
			thread.join();
		CHECK(bad.load() == 0);
	}
}

// The old archive system as a model: archives loaded one after the other into one tree, a path
// taken by the first archive unless the load overwrites. The mount plan with the index must pick
// the same archive for every path.
static void testVfsLegacyEquivalence()
{
	using namespace Engine::Vfs;
	std::printf("Vfs against the old load order\n");

	std::mt19937 random(20260930u);
	auto pick = [&random](int count) { return static_cast<int>(random() % static_cast<unsigned>(count)); };
	const std::vector<std::string> universe = {
		"data/ini/object/a.ini", "data/ini/object/b.ini", "data/ini/weapon.ini", "data/ini/gamedata.ini",
		"art/textures/a.dds", "art/textures/b.dds", "art/textures/c.tga", "art/terrain/t.dds",
		"data/german/art/textures/d.dds", "art/w3d/m.w3d", "maps/x/x.map", "audio/s.wav"};
	const std::vector<std::string> baseNames = {"INIZH.big", "Data\\INI\\INIZH.big", "A1.big", "a\\b.big",
		"ZH_Generals\\INI.big", "W3DZH.big", "Textures2.big"};
	const std::vector<std::string> archiveLetters = {"a.big", "B.big", "c1.big", "c\\d.big", "z.big"};

	int scenarios = 0, compared = 0, mismatches = 0;
	for (int scenario = 0; scenario < 400; ++scenario)
	{
		std::map<std::string, std::vector<std::string>> contents;	// archive name -> paths
		auto makeArchive = [&](const std::string &name, bool texturesOnly, bool contaminate)
		{
			std::vector<std::string> paths;
			for (const std::string &path : universe)
				if ((!texturesOnly || isTexturePackagePath(path)) && pick(3) == 0)
					paths.push_back(path);
			if (contaminate)
				paths.push_back("data/ini/weapon.ini");
			contents[name] = paths;
			return name;
		};
		auto someOf = [&](const std::vector<std::string> &names, const std::string &prefix, int max, bool textures)
		{
			std::vector<std::string> chosen;
			const int count = pick(max + 1);
			for (int i = 0; i < count && i < static_cast<int>(names.size()); ++i)
				chosen.push_back(makeArchive(prefix + names[static_cast<std::size_t>(pick(static_cast<int>(names.size())))], textures, textures && pick(5) == 0));
			std::sort(chosen.begin(), chosen.end());
			chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());
			return chosen;
		};

		MountInputs in;
		in.baseArchives = someOf(baseNames, "", 5, false);
		in.baseGameArchives = someOf(archiveLetters, "G:\\Generals\\", 2, false);
		in.tier = pick(3);
		in.packageArchives = someOf(archiveLetters, "Texturstufen\\Paket\\", 3, true);
		std::vector<std::string> modFolderArchives = someOf(archiveLetters, "C:\\Mods\\M\\", 3, false);
		std::string modArchive = pick(2) == 0 ? makeArchive("C:\\Mods\\single.big", false, false) : std::string();
		ModInput folderMod;
		if (!modFolderArchives.empty())
		{
			folderMod.folder = "C:\\Mods\\M\\";
			folderMod.folderArchives = modFolderArchives;
		}
		ModInput archiveMod;
		archiveMod.archive = modArchive;
		const bool archiveFirst = pick(2) == 0;
		if (archiveFirst && !modArchive.empty())
			in.mods.push_back(archiveMod);
		if (!folderMod.folder.empty())
			in.mods.push_back(folderMod);
		if (!archiveFirst && !modArchive.empty())
			in.mods.push_back(archiveMod);

		// old load order
		std::map<std::string, std::string> tree;
		auto load = [&](const std::string &name, bool overwrite)
		{
			for (const std::string &path : contents[name])
				if (overwrite || tree.find(path) == tree.end())
					tree[path] = name;
		};
		auto sortedNames = [](std::vector<std::string> names)
		{
			std::stable_sort(names.begin(), names.end(), LegacyOrderLess());
			return names;
		};
		for (const std::string &name : sortedNames(in.baseArchives))
			load(name, false);
		for (const std::string &name : sortedNames(in.baseGameArchives))
			load(name, false);
		if (in.tier > 0)
			for (const std::string &name : sortedNames(in.packageArchives))
			{
				const std::vector<std::string> &paths = contents[name];
				if (std::all_of(paths.begin(), paths.end(), [](const std::string &p) { return isTexturePackagePath(p); }))
					load(name, true);
			}
		if (!modArchive.empty())
			load(modArchive, true);
		for (const std::string &name : sortedNames(modFolderArchives))
			load(name, true);

		// new: plan, sources, index
		std::vector<std::unique_ptr<FolderSource>> sources;
		std::vector<Layer> layers;
		for (const Mount &mount : buildMountPlan(in))
		{
			sources.push_back(makeListSource(mount.path, contents[mount.path]));
			layers.push_back({sources.back().get(), mount.texturesOnly});
		}
		Index index;
		index.build(layers);

		++scenarios;
		for (const std::string &path : universe)
		{
			const auto old = tree.find(path);
			const Hit *hit = index.find(path);
			const std::string oldName = old == tree.end() ? std::string() : old->second;
			const std::string newName = hit == nullptr ? std::string() : hit->source->name();
			++compared;
			if (oldName != newName)
			{
				if (++mismatches <= 5)
					std::printf("  scenario %d, %s: old %s, new %s\n", scenario, path.c_str(), oldName.c_str(), newName.c_str());
			}
		}
	}
	CHECK(scenarios == 400 && compared == 400 * 12);
	CHECK(mismatches == 0);
}

// ------------------------------------------------------------------ .zip archives

struct ZipTestFile
{
	std::string name;
	std::string data;
	bool deflate = false;
	std::uint16_t flags = 0;
	int method = -1;				///< overrides the method written to both headers
	long long announced = -1;		///< overrides the unpacked size written to both headers
	std::uint32_t crcFlip = 0;		///< xor on the CRC
	std::string rawData;			///< written instead of the packed data when not empty
};

static void putLe16(std::vector<unsigned char> &out, unsigned value)
{
	out.push_back(static_cast<unsigned char>(value & 0xFF));
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
}

static void putLe32(std::vector<unsigned char> &out, std::uint32_t value)
{
	putLe16(out, value & 0xFFFF);
	putLe16(out, value >> 16);
}

static std::string rawDeflate(const std::string &data)
{
	z_stream stream;
	std::memset(&stream, 0, sizeof(stream));
	deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
	std::string out(deflateBound(&stream, static_cast<uLong>(data.size())) + 16, '\0');
	stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
	stream.avail_in = static_cast<uInt>(data.size());
	stream.next_out = reinterpret_cast<Bytef *>(out.data());
	stream.avail_out = static_cast<uInt>(out.size());
	deflate(&stream, Z_FINISH);
	out.resize(stream.total_out);
	deflateEnd(&stream);
	return out;
}

/// A .zip archive as zip tools write it: local headers with data, central directory, end record.
static std::vector<unsigned char> makeZip(const std::vector<ZipTestFile> &files, const std::string &comment = "")
{
	std::vector<unsigned char> out;
	std::vector<unsigned char> directory;
	for (const ZipTestFile &file : files)
	{
		const std::string packed = !file.rawData.empty() ? file.rawData : (file.deflate ? rawDeflate(file.data) : file.data);
		const unsigned method = file.method >= 0 ? static_cast<unsigned>(file.method) : (file.deflate ? 8u : 0u);
		const std::uint32_t crc = static_cast<std::uint32_t>(crc32(0L, reinterpret_cast<const Bytef *>(file.data.data()),
			static_cast<uInt>(file.data.size()))) ^ file.crcFlip;
		const std::uint32_t size = file.announced >= 0 ? static_cast<std::uint32_t>(file.announced) : static_cast<std::uint32_t>(file.data.size());
		const std::uint32_t offset = static_cast<std::uint32_t>(out.size());

		putLe32(out, 0x04034b50);
		putLe16(out, 20);
		putLe16(out, file.flags);
		putLe16(out, method);
		putLe16(out, 0);
		putLe16(out, 0);
		putLe32(out, crc);
		putLe32(out, static_cast<std::uint32_t>(packed.size()));
		putLe32(out, size);
		putLe16(out, static_cast<unsigned>(file.name.size()));
		putLe16(out, 0);
		out.insert(out.end(), file.name.begin(), file.name.end());
		out.insert(out.end(), packed.begin(), packed.end());

		putLe32(directory, 0x02014b50);
		putLe16(directory, 20);
		putLe16(directory, 20);
		putLe16(directory, file.flags);
		putLe16(directory, method);
		putLe16(directory, 0);
		putLe16(directory, 0);
		putLe32(directory, crc);
		putLe32(directory, static_cast<std::uint32_t>(packed.size()));
		putLe32(directory, size);
		putLe16(directory, static_cast<unsigned>(file.name.size()));
		putLe16(directory, 0);
		putLe16(directory, 0);
		putLe16(directory, 0);
		putLe16(directory, 0);
		putLe32(directory, 0);
		putLe32(directory, offset);
		directory.insert(directory.end(), file.name.begin(), file.name.end());
	}
	const std::uint32_t directoryOffset = static_cast<std::uint32_t>(out.size());
	out.insert(out.end(), directory.begin(), directory.end());
	putLe32(out, 0x06054b50);
	putLe16(out, 0);
	putLe16(out, 0);
	putLe16(out, static_cast<unsigned>(files.size()));
	putLe16(out, static_cast<unsigned>(files.size()));
	putLe32(out, static_cast<std::uint32_t>(directory.size()));
	putLe32(out, directoryOffset);
	putLe16(out, static_cast<unsigned>(comment.size()));
	out.insert(out.end(), comment.begin(), comment.end());
	return out;
}

static Engine::ZipArchiveError readZip(const std::vector<unsigned char> &archive, std::vector<Engine::ZipArchiveEntry> &entries,
	std::vector<Engine::ZipArchiveSkipped> *skipped = nullptr)
{
	auto bytes = std::make_shared<const std::vector<unsigned char>>(archive);
	return Engine::readZipArchiveDirectory(readAtMemory(bytes), bytes->size(), entries, skipped);
}

static Engine::ZipArchiveError extractZip(const std::vector<unsigned char> &archive, const Engine::ZipArchiveEntry &entry, std::string &text)
{
	auto bytes = std::make_shared<const std::vector<unsigned char>>(archive);
	std::vector<unsigned char> data;
	const Engine::ZipArchiveError error = Engine::extractZipEntry(readAtMemory(bytes), bytes->size(), entry, data);
	text.assign(data.begin(), data.end());
	return error;
}

static const Engine::ZipArchiveSkipped *findSkipped(const std::vector<Engine::ZipArchiveSkipped> &skipped, const std::string &path)
{
	for (const Engine::ZipArchiveSkipped &s : skipped)
		if (s.path == path)
			return &s;
	return nullptr;
}

static void testZipArchive()
{
	using Engine::ZipArchiveError;
	std::printf("ZIP archive\n");

	std::string big(200000, '\0');
	for (std::size_t i = 0; i < big.size(); ++i)
		big[i] = static_cast<char>("terrain"[i % 7] + (i / 5000) % 3);
	ZipTestFile stored{"Art/Terrain/a.dds", "stored bytes"};
	ZipTestFile packed{"Art\\Terrain\\B.DDS", big};
	packed.deflate = true;
	ZipTestFile empty{"Data/empty.ini", ""};
	empty.deflate = true;
	ZipTestFile folder{"Art/Terrain/", ""};
	std::vector<unsigned char> archive = makeZip({folder, stored, packed, empty}, "a comment");

	std::vector<Engine::ZipArchiveEntry> entries;
	std::vector<Engine::ZipArchiveSkipped> skipped;
	CHECK(readZip(archive, entries, &skipped) == ZipArchiveError::None);
	CHECK(entries.size() == 3 && skipped.empty());	// the folder entry is left out silently
	if (entries.size() == 3)
	{
		CHECK(entries[0].path == "Art/Terrain/a.dds" && entries[0].method == Engine::ZIP_METHOD_STORED && entries[0].size == 12);
		CHECK(entries[1].method == Engine::ZIP_METHOD_DEFLATE && entries[1].size == big.size() && entries[1].compressedSize < big.size());
		std::string text;
		CHECK(extractZip(archive, entries[0], text) == ZipArchiveError::None && text == "stored bytes");
		CHECK(extractZip(archive, entries[1], text) == ZipArchiveError::None && text == big);
		CHECK(extractZip(archive, entries[2], text) == ZipArchiveError::None && text.empty());
		std::uint64_t dataOffset = 0;
		auto bytes = std::make_shared<const std::vector<unsigned char>>(archive);
		CHECK(Engine::locateZipEntryData(readAtMemory(bytes), bytes->size(), entries[0], dataOffset) == ZipArchiveError::None
			&& std::memcmp(archive.data() + dataOffset, "stored bytes", 12) == 0);

		// damaged entries: the directory still reads, unpacking fails
		std::vector<unsigned char> damaged = archive;
		damaged[entries[1].headerOffset] = 'X';
		CHECK(extractZip(damaged, entries[1], text) == ZipArchiveError::BadLocalHeader && text.empty());
		damaged = archive;
		damaged[dataOffset] ^= 0x20;
		CHECK(extractZip(damaged, entries[0], text) == ZipArchiveError::CrcMismatch);
		Engine::ZipArchiveEntry wrong = entries[0];
		wrong.headerOffset = archive.size() - 10;
		CHECK(extractZip(archive, wrong, text) == ZipArchiveError::EntryOutsideArchive);
	}

	// per entry: unsafe names, encryption, other methods, sizes
	ZipTestFile parent{"../evil.ini", "x"};
	ZipTestFile inner{"Data\\..\\..\\evil.ini", "x"};
	ZipTestFile absolute{"/etc/evil", "x"};
	ZipTestFile backslash{"\\evil.ini", "x"};
	ZipTestFile drive{"C:evil.ini", "x"};
	ZipTestFile control{"a\x01.ini", "x"};
	ZipTestFile encrypted{"secret.ini", "x"};
	encrypted.flags = 1;
	ZipTestFile bzip{"bzip.ini", "x"};
	bzip.method = 12;
	ZipTestFile longName{std::string(300, 'n'), "x"};
	ZipTestFile huge{"huge.dds", "x"};
	huge.announced = 0x7FFFFFFF;
	ZipTestFile ratio{"ratio.dds", "abc"};
	ratio.deflate = true;
	ratio.announced = 5000000;
	ZipTestFile storedSize{"stored.dds", "abc"};
	storedSize.announced = 4;
	ZipTestFile good{"Data/INI/good.ini", "good"};
	archive = makeZip({parent, inner, absolute, backslash, drive, control, encrypted, bzip, longName, huge, ratio, storedSize, good});
	CHECK(readZip(archive, entries, &skipped) == ZipArchiveError::None);
	CHECK(entries.size() == 1 && entries[0].path == "Data/INI/good.ini");
	CHECK(skipped.size() == 12);
	for (const char *name : {"../evil.ini", "Data\\..\\..\\evil.ini", "/etc/evil", "\\evil.ini", "C:evil.ini", "a\x01.ini"})
	{
		const Engine::ZipArchiveSkipped *s = findSkipped(skipped, name);
		CHECK(s != nullptr && s->reason == ZipArchiveError::BadPath);
	}
	CHECK(findSkipped(skipped, "secret.ini") && findSkipped(skipped, "secret.ini")->reason == ZipArchiveError::Encrypted);
	CHECK(findSkipped(skipped, "bzip.ini") && findSkipped(skipped, "bzip.ini")->reason == ZipArchiveError::UnsupportedMethod);
	CHECK(findSkipped(skipped, std::string(Engine::ZIP_ARCHIVE_MAX_NAME, 'n'))
		&& findSkipped(skipped, std::string(Engine::ZIP_ARCHIVE_MAX_NAME, 'n'))->reason == ZipArchiveError::NameTooLong);
	CHECK(findSkipped(skipped, "huge.dds") && findSkipped(skipped, "huge.dds")->reason == ZipArchiveError::EntryTooLarge);
	CHECK(findSkipped(skipped, "ratio.dds") && findSkipped(skipped, "ratio.dds")->reason == ZipArchiveError::ImplausibleRatio);
	CHECK(findSkipped(skipped, "stored.dds") && findSkipped(skipped, "stored.dds")->reason == ZipArchiveError::SizeMismatch);

	// zip bomb: the stream unpacks to far more than announced; unpacking stops at the announced size
	ZipTestFile bomb{"bomb.dds", std::string(8 * 1024 * 1024, '\0')};
	bomb.deflate = true;
	bomb.announced = 4096;
	ZipTestFile shortStream{"short.dds", std::string(1000, 'a')};
	shortStream.deflate = true;
	shortStream.announced = 2000;
	ZipTestFile corrupt{"corrupt.dds", "corrupt corrupt corrupt"};
	corrupt.deflate = true;
	corrupt.rawData = std::string("\xff\xff\xff\xff\xff\xff", 6);
	std::string noise(20000, '\0');
	std::uint32_t seed = 12345;
	for (char &c : noise)
	{
		seed = seed * 1103515245u + 12345u;
		c = static_cast<char>(seed >> 24);
	}
	ZipTestFile cut{"cut.dds", noise};
	cut.deflate = true;
	cut.rawData = rawDeflate(noise).substr(0, 1000);
	archive = makeZip({bomb, shortStream, corrupt, cut});
	CHECK(readZip(archive, entries, &skipped) == ZipArchiveError::None && entries.size() == 4);
	if (entries.size() == 4)
	{
		std::string text;
		CHECK(entries[0].compressedSize < 16384);
		CHECK(extractZip(archive, entries[0], text) == ZipArchiveError::SizeMismatch && text.empty());
		CHECK(extractZip(archive, entries[1], text) == ZipArchiveError::SizeMismatch);
		CHECK(extractZip(archive, entries[2], text) == ZipArchiveError::CorruptData);
		CHECK(extractZip(archive, entries[3], text) == ZipArchiveError::CorruptData);
	}

	// broken archives
	archive = makeZip({stored, packed});
	CHECK(readZip(std::vector<unsigned char>(archive.begin(), archive.begin() + 40), entries) == ZipArchiveError::NotZip);
	CHECK(readZip(std::vector<unsigned char>(archive.begin(), archive.end() - 5), entries) == ZipArchiveError::NotZip);
	CHECK(readZip(std::vector<unsigned char>(), entries) == ZipArchiveError::NotZip);
	CHECK(readZip(std::vector<unsigned char>(5000, 'P'), entries) == ZipArchiveError::NotZip);
	const std::size_t end = archive.size() - 22;
	std::vector<unsigned char> broken = archive;
	broken[end + 4] = 1;										// disk number
	CHECK(readZip(broken, entries) == ZipArchiveError::MultiDisk);
	broken = archive;
	broken[end + 10] = 0xFF;									// 0xFFFF entries: Zip64
	broken[end + 11] = 0xFF;
	CHECK(readZip(broken, entries) == ZipArchiveError::Zip64);
	broken = archive;
	broken[end + 10] = 50;										// more entries than the directory holds
	broken[end + 8] = 50;
	CHECK(readZip(broken, entries) == ZipArchiveError::BadEntryCount);
	broken = archive;
	broken[end + 19] = 0x10;									// directory offset far beyond the end
	CHECK(readZip(broken, entries) == ZipArchiveError::BadDirectory);
	broken = archive;
	const std::size_t directoryOffset = broken[end + 16] | (broken[end + 17] << 8);
	broken[directoryOffset] = 'X';								// first directory record
	CHECK(readZip(broken, entries) == ZipArchiveError::BadDirectory);
	broken = archive;
	broken[directoryOffset + 28] = 0xFF;						// name runs past the directory
	CHECK(readZip(broken, entries) == ZipArchiveError::TruncatedDirectory);
	// data in front of the archive (self-extracting): the directory is not where the end record says
	broken.assign(100, 'x');
	broken.insert(broken.end(), archive.begin(), archive.end());
	CHECK(readZip(broken, entries) == ZipArchiveError::BadDirectory);
	CHECK(std::strcmp(Engine::zipArchiveErrorText(ZipArchiveError::Zip64), "Zip64 not supported") == 0);
}

static void testVfsZipSource()
{
	using namespace Engine::Vfs;
	std::printf("Vfs zip source\n");

	std::string big(70000, 'q');
	for (std::size_t i = 0; i < big.size(); i += 7)
		big[i] = static_cast<char>('a' + i % 26);
	ZipTestFile stored{"Art\\Terrain\\A.dds", "0123456789"};
	ZipTestFile packed{"art/terrain/b.dds", big};
	packed.deflate = true;
	ZipTestFile again{"ART/terrain/a.DDS", "later"};	// same key: the later entry counts
	ZipTestFile evil{"../x.ini", "x"};
	auto bytes = std::make_shared<const std::vector<unsigned char>>(makeZip({stored, packed, again, evil}));

	Engine::ZipArchiveError error = Engine::ZipArchiveError::NotZip;
	std::unique_ptr<ZipSource> zip = ZipSource::open("Mod.zip", readAtMemory(bytes), bytes->size(), &error);
	CHECK(zip != nullptr && error == Engine::ZipArchiveError::None);
	if (zip)
	{
		CHECK(zip->name() == "Mod.zip" && zip->entries().size() == 2 && zip->rejected().size() == 1);
		const Entry *a = zip->find("art\\terrain\\a.dds");
		const Entry *b = zip->find("Art/Terrain/B.DDS");
		CHECK(a != nullptr && b != nullptr && zip->find("art/terrain/c.dds") == nullptr && zip->find("..\\a") == nullptr);
		if (a != nullptr && b != nullptr)
		{
			CHECK(readAll(*zip, *a) == "later");
			CHECK(readAll(*zip, *b) == big);
			char part[8] = {};
			CHECK(zip->read(*a, 1, part, 3) == 3 && std::memcmp(part, "ate", 3) == 0);
			CHECK(zip->read(*b, 69990, part, 8) == 8 && std::memcmp(part, big.data() + 69990, 8) == 0);
			CHECK(zip->read(*b, 70000, part, 8) == 0);
			std::vector<unsigned char> data;
			CHECK(zip->readEntry(*b, data) == Engine::ZipArchiveError::None && data.size() == big.size());
		}
	}

	std::vector<unsigned char> damaged = *bytes;
	damaged[damaged.size() - 22] = 'X';
	CHECK(ZipSource::open("bad.zip", readAtMemory(std::make_shared<const std::vector<unsigned char>>(damaged)),
		damaged.size(), &error) == nullptr && error == Engine::ZipArchiveError::NotZip);
}

// One mod folder with loose files and archives (.big and .zip), a mod archive, a base archive: the
// loose file beats every archive, the later archive of the folder beats the earlier, the folder
// beats the -mod archive, every mod beats the base archives.
static std::filesystem::path makeTestDir(const char *name)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path dir = fs::temp_directory_path(ec) / (std::string(name) + "_" + std::to_string(std::rand()));
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);
	return dir;
}

static bool writeBytes(const std::filesystem::path &path, const std::vector<unsigned char> &bytes)
{
	std::FILE *fp = std::fopen(path.string().c_str(), "wb");
	if (fp == nullptr)
		return false;
	const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), fp) == bytes.size();
	return std::fclose(fp) == 0 && ok;
}

static void testPositionalFile()
{
	using Engine::PositionalFile;
	std::printf("PositionalFile\n");
	const std::filesystem::path dir = makeTestDir("enginetests_positional");
	std::vector<unsigned char> bytes(300000);
	for (std::size_t i = 0; i < bytes.size(); ++i)
		bytes[i] = static_cast<unsigned char>((i * 131u) ^ (i >> 8));
	const std::filesystem::path path = dir / "data.bin";
	CHECK(writeBytes(path, bytes));

	PositionalFile file;
	CHECK(!file.isOpen());
	CHECK(file.readAt(0, bytes.data(), 1) == 0);
	CHECK(!file.open((dir / "missing.bin").string()));
	CHECK(file.open(path.string()));
	CHECK(file.isOpen());
	CHECK(file.size() == bytes.size());

	// same bytes as reading front to back, at any offset and in any order
	std::vector<unsigned char> got(70000);
	const std::size_t offsets[] = {250000, 0, 123457, 229999, 1};
	for (std::size_t offset : offsets)
	{
		const std::size_t n = file.readAt(offset, got.data(), got.size());
		const std::size_t expected = std::min(got.size(), bytes.size() - offset);
		CHECK(n == expected);
		CHECK(std::memcmp(got.data(), bytes.data() + offset, n) == 0);
	}
	CHECK(file.readAt(bytes.size(), got.data(), 10) == 0);
	CHECK(file.readAt(bytes.size() + 1000, got.data(), 10) == 0);
	CHECK(file.readAt(bytes.size() - 3, got.data(), 10) == 3);

	// several threads on one open file
	std::atomic<int> wrong{0};
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; ++t)
		threads.emplace_back([&, t]()
		{
			std::vector<unsigned char> buffer(4096);
			std::uint32_t seed = 12345u + static_cast<std::uint32_t>(t);
			for (int i = 0; i < 500; ++i)
			{
				seed = seed * 1664525u + 1013904223u;
				const std::size_t offset = seed % (bytes.size() - buffer.size());
				if (file.readAt(offset, buffer.data(), buffer.size()) != buffer.size()
					|| std::memcmp(buffer.data(), bytes.data() + offset, buffer.size()) != 0)
					++wrong;
			}
		});
	for (std::thread &thread : threads)
		thread.join();
	CHECK(wrong.load() == 0);

	file.close();
	CHECK(!file.isOpen());
	CHECK(file.size() == 0);
	CHECK(file.open(path.string(), PositionalFile::Pattern::Sequential));
	CHECK(file.size() == bytes.size());
	file.close();
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}

static void testVfsPrefetchPlan()
{
	using namespace Engine::Vfs;
	std::printf("Vfs prefetch plan\n");
	auto bigSource = [](const std::string &name, const std::vector<std::pair<std::string, std::string>> &files)
	{
		auto bytes = std::make_shared<const std::vector<unsigned char>>(makeBigWithData(files));
		return BigSource::open(name, readAtMemory(bytes), bytes->size());
	};
	const std::unique_ptr<BigSource> mod = bigSource("Mods\\Mod.big", {{"Data\\INI\\a.ini", "A-mod"}, {"Data\\INI\\c.ini", "C-mod"}});
	const std::unique_ptr<BigSource> ini = bigSource("INIZH.big", {{"Data\\INI\\a.ini", std::string(10, 'a')},
		{"Data\\INI\\b.ini", std::string(20, 'b')}, {"Data\\INI\\c.ini", std::string(30, 'c')}, {"Data\\INI\\d.ini", std::string(40, 'd')}});
	const std::unique_ptr<BigSource> audio = bigSource("ZH_Generals\\AudioEnglish.big", {{"Data\\INI\\e.ini", "e"}});
	const std::unique_ptr<BigSource> sounds = bigSource("SoundsOnly.big", {{"Data\\Audio\\s.wav", "s"}, {"Data\\Audio\\t.MP3", "t"}});
	const std::unique_ptr<BigSource> covered = bigSource("Covered.big", {{"Data\\INI\\b.ini", "b"}});
	const std::unique_ptr<FolderSource> folder = makeListSource("Mods\\Folder\\", {"Art\\x.dds", "Art\\y.dds"});
	CHECK(mod && ini && audio && sounds && covered);
	if (!mod || !ini || !audio || !sounds || !covered)
		return;

	Snapshot snapshot;
	snapshot.layers = {{folder.get(), false, "Mods\\Folder\\"}, {mod.get(), false, "Mods\\Mod.big"}, {ini.get(), false, "INIZH.big"},
		{audio.get(), false, "ZH_Generals\\AudioEnglish.big"}, {sounds.get(), false, "SoundsOnly.big"}, {covered.get(), false, "Covered.big"}};
	snapshot.index.build(snapshot.layers);

	// served: b and d of INIZH (a and c come from the mod); the gap of c is read through
	std::vector<PrefetchItem> plan = prefetchPlan(snapshot);
	CHECK(plan.size() == 3);
	if (plan.size() == 3)
	{
		CHECK(plan[0].layer == 0 && plan[0].files.size() == 2 && plan[0].ranges.empty());
		CHECK(plan[1].layer == 1 && plan[1].files.size() == 2 && plan[1].ranges.size() == 1 && plan[1].bytes == 10);
		CHECK(plan[2].layer == 2 && plan[2].files.size() == 2);
		const Entry *b = ini->find("data/ini/b.ini");
		const Entry *d = ini->find("data/ini/d.ini");
		CHECK(plan[2].ranges.size() == 1 && plan[2].ranges[0].offset == b->offset
			&& plan[2].ranges[0].size == d->offset + d->size - b->offset && plan[2].bytes == 90);
		CHECK(plan[2].files[0] == b && plan[2].files[1] == d);
	}

	// without reading through gaps: one range per served run
	plan = prefetchPlan(snapshot, 0);
	CHECK(plan.size() == 3 && plan[2].ranges.size() == 2 && plan[2].bytes == 60);

	// the sound archive name counts only by its file name; a name without layer name falls back to the source
	snapshot.layers[3].name.clear();
	snapshot.layers[4].name = "Data\\audio\\SoundsOnly.big";
	snapshot.index.build(snapshot.layers);
	plan = prefetchPlan(snapshot);
	CHECK(plan.size() == 3);
}

// Readers look files up and read them while a mounter adds and removes layers. Every read checks
// that the bytes belong to the layer the lookup named. Run under ThreadSanitizer in the gate.
static void runVfsLoad(int accesses, int readers)
{
	using namespace Engine::Vfs;
	const int ARCHIVES = 4, FILES = 40;
	auto content = [](int archive, int file)
	{
		std::string text = "A" + std::to_string(archive) + ":" + std::to_string(file) + ":";
		const std::size_t size = 500 + static_cast<std::size_t>((archive * 7 + file) * 31);
		while (text.size() < size)
			text += static_cast<char>('a' + (text.size() + static_cast<std::size_t>(archive)) % 26);
		return text;
	};

	const std::filesystem::path dir = makeTestDir("enginetests_vfslast");
	std::vector<std::shared_ptr<Engine::PositionalFile>> files;
	std::vector<std::unique_ptr<BigSource>> sources;
	for (int a = 0; a < ARCHIVES; ++a)
	{
		// every archive holds every other file plus its own block, so layers cover each other in part
		std::vector<std::pair<std::string, std::string>> entries;
		for (int f = 0; f < FILES; ++f)
			if (f % 2 == a % 2 || f / 10 == a)
				entries.push_back({"Data\\Last\\f" + std::to_string(f) + ".bin", content(a, f)});
		const std::filesystem::path path = dir / ("Last" + std::to_string(a) + ".big");
		CHECK(writeBytes(path, makeBigWithData(entries)));
		auto file = std::make_shared<Engine::PositionalFile>();
		CHECK(file->open(path.string()));
		sources.push_back(BigSource::open(path.string(),
			[file](std::uint64_t offset, void *dest, std::size_t size) { return file->readAt(offset, dest, size); }, file->size()));
		CHECK(sources.back() != nullptr);
		files.push_back(file);
	}
	if (std::any_of(sources.begin(), sources.end(), [](const auto &source) { return source == nullptr; }))
		return;

	auto layerOf = [&](int a)
	{
		Layer layer;
		layer.source = sources[static_cast<std::size_t>(a)].get();
		layer.name = sources[static_cast<std::size_t>(a)]->name();
		layer.owner = sources[static_cast<std::size_t>(a)].get();
		layer.flags = a;
		return layer;
	};
	LiveIndex live;
	for (int a = 0; a < 3; ++a)
		live.addBottom(layerOf(a));
	CHECK(live.layerCount() == 3);

	std::atomic<bool> stop{false};
	std::atomic<int> wrong{0}, found{0}, missing{0};
	std::thread mounter([&]()
	{
		for (int round = 0; !stop.load(); ++round)
		{
			live.addTop(layerOf(3));
			std::this_thread::yield();
			live.removeOwner(sources[1].get());
			std::this_thread::yield();
			live.removeOwner(sources[3].get());
			live.addBottom(layerOf(1));
			std::this_thread::yield();
		}
	});
	std::vector<std::thread> threads;
	const int perReader = accesses / readers;
	for (int t = 0; t < readers; ++t)
		threads.emplace_back([&, t]()
		{
			std::uint32_t seed = 777u + static_cast<std::uint32_t>(t) * 31u;
			std::vector<char> buffer;
			for (int i = 0; i < perReader; ++i)
			{
				seed = seed * 1664525u + 1013904223u;
				const int f = static_cast<int>((seed >> 8) % FILES);
				const std::shared_ptr<const Snapshot> snapshot = live.current();
				const Hit *hit = snapshot->index.find("data/last/f" + std::to_string(f) + ".bin");
				if (hit == nullptr)
				{
					++missing;
					continue;
				}
				++found;
				const int archive = snapshot->layers[hit->layer].flags;
				const std::string expected = content(archive, f);
				buffer.assign(static_cast<std::size_t>(hit->entry->size), 0);
				if (hit->source->read(*hit->entry, 0, buffer.data(), buffer.size()) != expected.size()
					|| std::memcmp(buffer.data(), expected.data(), expected.size()) != 0)
					++wrong;
			}
		});
	for (std::thread &thread : threads)
		thread.join();
	stop.store(true);
	mounter.join();

	CHECK(wrong.load() == 0);
	CHECK(found.load() + missing.load() == perReader * readers);
	CHECK(found.load() > 0);
	std::printf("  %d accesses by %d readers: %d found, %d not mounted at the time, %d wrong\n", perReader * readers, readers,
		found.load(), missing.load(), wrong.load());

	sources.clear();
	files.clear();
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}

static void testVfsLoad()
{
	std::printf("Vfs under load\n");
	runVfsLoad(2000, 4);
}

static void testModCatalog()
{
	using namespace Engine;
	std::printf("ModCatalog\n");

	// mod.txt
	ModInfo info;
	info.name = "Fallback";
	parseModManifest("\xEF\xBB\xBF# comment\r\nName = Big Mod\r\n; other comment\r\nVersion=1.2\r\nAutor = Someone\r\nSpiel = ZH\r\n"
		"Beschreibung = More units\r\n", info);
	CHECK(info.hasManifest && info.title == "Big Mod" && info.version == "1.2" && info.author == "Someone");
	CHECK(info.game == ModGame::ZeroHour && info.description == "More units" && info.manifestError.empty());
	ModInfo english;
	english.name = "Plain";
	parseModManifest("author=A\ngame=Generals\ndescription=d\ncolour = red\nno equals here\n", english);
	CHECK(english.title == "Plain" && english.author == "A" && english.game == ModGame::Generals && english.description == "d");
	CHECK(english.manifestError.find("colour") != std::string::npos && english.manifestError.find("line 5") != std::string::npos);
	ModInfo badGame;
	parseModManifest("Game = Red Alert\n", badGame);
	CHECK(badGame.game == ModGame::Any && !badGame.manifestError.empty());

	// the active list
	const std::vector<std::string> names = splitModList(" A ; b;;a ; C;");
	CHECK(names.size() == 3 && names[0] == "A" && names[1] == "b" && names[2] == "C");
	CHECK(joinModList(names) == "A;b;C");
	CHECK(splitModList("").empty() && joinModList({}).empty());

	// the mods folder: folders, .big and .zip, mod.txt from each
	namespace fs = std::filesystem;
	const fs::path dir = makeTestDir("enginetests_mods");
	std::error_code ec;
	fs::create_directories(dir / "FolderMod", ec);
	fs::create_directories(dir / "Empty", ec);
	fs::create_directories(dir / "Texturstufen", ec);
	{
		std::FILE *fp = std::fopen((dir / "FolderMod" / "mod.txt").string().c_str(), "wb");
		CHECK(fp != nullptr);
		if (fp)
		{
			std::fputs("Name = Folder Mod\nGame = ZeroHour\n", fp);
			std::fclose(fp);
		}
	}
	CHECK(writeBytes(dir / "Archive.big", makeBigWithData({{"mod.txt", "Name = Big Archive\nGame = Generals\n"}, {"Data\\INI\\x.ini", "x"}})));
	ZipTestFile manifest{"mod.txt", "Name = Zipped\nVersion = 3\n"};
	manifest.deflate = true;
	CHECK(writeBytes(dir / "zipped.zip", makeZip({manifest, ZipTestFile{"Art/a.dds", "a"}})));
	CHECK(writeBytes(dir / "readme.txt", {'h', 'i'}));
	CHECK(writeBytes(dir / "Broken.big", {'n', 'o'}));

	const std::vector<ModInfo> catalog = scanModFolder(dir.string());
	CHECK(catalog.size() == 5);
	if (catalog.size() == 5)
	{
		// sorted by name without case: Archive.big, Broken.big, Empty, FolderMod, zipped.zip
		CHECK(catalog[0].name == "Archive.big" && catalog[0].kind == ModKind::Big && catalog[0].title == "Big Archive"
			&& catalog[0].game == ModGame::Generals);
		CHECK(catalog[1].name == "Broken.big" && !catalog[1].hasManifest && catalog[1].title == "Broken.big");
		CHECK(catalog[2].name == "Empty" && catalog[2].kind == ModKind::Folder && !catalog[2].hasManifest);
		CHECK(catalog[3].name == "FolderMod" && catalog[3].title == "Folder Mod" && catalog[3].game == ModGame::ZeroHour);
		CHECK(!catalog[3].path.empty() && (catalog[3].path.back() == '/' || catalog[3].path.back() == '\\'));
		CHECK(catalog[4].name == "zipped.zip" && catalog[4].kind == ModKind::Zip && catalog[4].title == "Zipped"
			&& catalog[4].version == "3");
	}

	// the list against the catalog
	const std::vector<ActiveMod> active = resolveModList(catalog, splitModList("foldermod;archive.big;gone"), ModGame::ZeroHour);
	CHECK(active.size() == 3);
	if (active.size() == 3)
	{
		CHECK(active[0].state == ModState::Ready && active[0].info != nullptr && active[0].info->name == "FolderMod");
		CHECK(active[1].state == ModState::OtherGame);
		CHECK(active[2].state == ModState::Missing && active[2].info == nullptr);
	}
	CHECK(resolveModList(catalog, {"Archive.big"}, ModGame::Generals)[0].state == ModState::Ready);
	CHECK(std::string(modStateName(ModState::Missing)) == "missing");
	CHECK(scanModFolder((dir / "not there").string()).empty());
	fs::remove_all(dir, ec);

	// a list ordered in the menu keeps its order, -mod keeps the folder-above-archive rule
	std::vector<Vfs::ModInput> pair(2);
	pair[0].folder = "Mods\\A\\";
	pair[1].archive = "Mods\\b.big";
	CHECK(Vfs::modPriority(pair) == std::vector<int>({0, 1}));
	CHECK(Vfs::modPriority(pair, true) == std::vector<int>({1, 0}));

	// starting again: without -mod (any case) and its value, quoted as CommandLineToArgvW reads it
	CHECK(restartCommandLine({L"C:\\Games\\GZHNeoZH.exe", L"-win", L"-mod", L"Mods\\a.big", L"-xres", L"800", L"-MOD", L"b"})
		== L"C:\\Games\\GZHNeoZH.exe -win -xres 800");
	CHECK(restartCommandLine({L"C:\\Program Files\\Game\\GZHNeoZH.exe", L"-win"}) == L"\"C:\\Program Files\\Game\\GZHNeoZH.exe\" -win");
	CHECK(restartCommandLine({L"x.exe", L"-benutzerdaten", L"C:\\My Data\\"}) == L"x.exe -benutzerdaten \"C:\\My Data\\\\\"");
	CHECK(restartCommandLine({L"x.exe", L"a\"b", L""}) == L"x.exe \"a\\\"b\" \"\"");
	CHECK(restartCommandLine({L"x.exe", L"-mod"}) == L"x.exe");

	// mod list of the game options, replays and save games
	{
		std::vector<Engine::ModTagEntry> mods = {{"Base", "1.2"}, {"My Mod;x=1", ""}, {"+5", "a@b"}};
		const std::string tag = Engine::encodeModTag(mods, 200);
		CHECK(tag == "Base@1.2,My%20Mod%3Bx%3D1,%2B5@a%40b");
		CHECK(tag.find_first_of(";=: ") == std::string::npos);
		int omitted = -1;
		const std::vector<Engine::ModTagEntry> back = Engine::decodeModTag(tag, &omitted);
		CHECK(back.size() == 3 && omitted == 0 && back[1].name == "My Mod;x=1" && back[2].name == "+5" && back[2].version == "a@b");
		CHECK(Engine::sameModTags(tag, "base@1.2,My%20Mod%3Bx%3D1,%2B5@a%40b"));
		CHECK(!Engine::sameModTags(tag, "Base@1.3,My%20Mod%3Bx%3D1,%2B5@a%40b"));
		CHECK(!Engine::sameModTags("Base", "Base,Other") && Engine::sameModTags("", ""));
		const std::string cut = Engine::encodeModTag(mods, 20);
		CHECK(cut.size() <= 20 && cut == "Base@1.2,+2");
		CHECK(Engine::decodeModTag(cut, &omitted).size() == 1 && omitted == 2);
		CHECK(Engine::encodeModTag(mods, 2) == "+3");
		CHECK(Engine::describeModTag(cut) == "Base 1.2, ... (+2)");
		CHECK(Engine::describeModTag("").empty() && Engine::encodeModTag({}, 10).empty());
		CHECK(Engine::decodeModTag("A%Z1,%4", &omitted).size() == 2 && Engine::decodeModTag("A%Z1")[0].name == "A%Z1");
	}
}

static void testVfsModRanks()
{
	using namespace Engine::Vfs;
	std::printf("Vfs mod ranks\n");

	auto baseBytes = std::make_shared<const std::vector<unsigned char>>(makeBigWithData({
		{"Art\\Terrain\\a.dds", "base"}, {"Art\\Terrain\\b.dds", "base"}, {"Art\\Terrain\\c.dds", "base"},
		{"Art\\Terrain\\d.dds", "base"}, {"Art\\Terrain\\e.dds", "base"}}));
	auto modBigBytes = std::make_shared<const std::vector<unsigned char>>(makeBigWithData({
		{"Art\\Terrain\\a.dds", "mod big"}, {"Art\\Terrain\\b.dds", "mod big"}, {"Art\\Terrain\\c.dds", "mod big"}}));
	ZipTestFile za{"Art/Terrain/a.dds", "mod zip"};
	ZipTestFile zb{"Art/Terrain/b.dds", "mod zip"};
	zb.deflate = true;
	auto modZipBytes = std::make_shared<const std::vector<unsigned char>>(makeZip({za, zb}));
	ZipTestFile single{"Art/Terrain/d.dds", "single zip"};
	ZipTestFile singleA{"Art/Terrain/a.dds", "single zip"};
	auto singleBytes = std::make_shared<const std::vector<unsigned char>>(makeZip({single, singleA}));

	const std::map<std::string, std::string> disk = {{"Art\\Terrain\\a.dds", "loose"}, {"Readme.txt", "text"}};
	std::vector<FolderSource::File> files;
	for (const auto &file : disk)
		files.push_back({file.first, file.second.size()});
	auto folder = std::make_unique<FolderSource>("M:\\Mod\\", files,
		[&disk](const std::string &path, std::uint64_t offset, void *dest, std::size_t size) -> std::size_t
		{
			const std::string &text = disk.at(path);
			const std::size_t n = std::min<std::size_t>(size, text.size() - static_cast<std::size_t>(offset));
			std::memcpy(dest, text.data() + offset, n);
			return n;
		});

	std::map<std::string, std::unique_ptr<Source>> sources;
	sources["Base.big"] = BigSource::open("Base.big", readAtMemory(baseBytes), baseBytes->size());
	sources["M:\\Mod\\a.big"] = BigSource::open("M:\\Mod\\a.big", readAtMemory(modBigBytes), modBigBytes->size());
	sources["M:\\Mod\\b.zip"] = ZipSource::open("M:\\Mod\\b.zip", readAtMemory(modZipBytes), modZipBytes->size());
	sources["S:\\Single.zip"] = ZipSource::open("S:\\Single.zip", readAtMemory(singleBytes), singleBytes->size());
	sources["M:\\Mod\\"] = std::move(folder);
	CHECK(std::all_of(sources.begin(), sources.end(), [](const auto &s) { return s.second != nullptr; }));

	ModInput folderMod;
	folderMod.folder = "M:\\Mod\\";
	folderMod.folderArchives = {"M:\\Mod\\b.zip", "M:\\Mod\\a.big"};
	folderMod.hasLooseFiles = true;
	ModInput archiveMod;
	archiveMod.archive = "S:\\Single.zip";
	for (int order = 0; order < 2; ++order)
	{
		MountInputs in;
		in.baseArchives = {"Base.big"};
		in.mods = order == 0 ? std::vector<ModInput>{folderMod, archiveMod} : std::vector<ModInput>{archiveMod, folderMod};
		std::vector<Layer> layers;
		for (const Mount &mount : buildMountPlan(in))
			layers.push_back({sources[mount.path].get(), mount.texturesOnly});
		CHECK(layers.size() == 5);
		Index index;
		index.build(layers);

		auto from = [&index](const char *path) -> std::string
		{
			const Hit *hit = index.find(path);
			return hit == nullptr ? std::string("-") : hit->source->name() + ":" + readAll(*hit->source, *hit->entry);
		};
		CHECK(from("art/terrain/a.dds") == "M:\\Mod\\:loose");				// loose file over all archives
		CHECK(from("ART\\TERRAIN\\B.DDS") == "M:\\Mod\\b.zip:mod zip");		// b.zip after a.big in name order
		CHECK(from("art/terrain/c.dds") == "M:\\Mod\\a.big:mod big");
		CHECK(from("art/terrain/d.dds") == "S:\\Single.zip:single zip");	// -mod archive below the folder, above the game
		CHECK(from("art/terrain/e.dds") == "Base.big:base");
		CHECK(from("readme.txt") == "M:\\Mod\\:text");
	}

	// Priority of several mods: the last one named on top; only a folder named before an archive keeps
	// the old order (folder above archive).
	ModInput a, b, c;
	a.folder = "A\\";
	b.archive = "B.zip";
	c.folder = "C\\";
	CHECK((modPriority({a, b, c}) == std::vector<int>{2, 1, 0}));
	CHECK((modPriority({a, b}) == std::vector<int>{0, 1}));
	CHECK((modPriority({b, a}) == std::vector<int>{1, 0}));
	CHECK((modPriority({a, c}) == std::vector<int>{1, 0}));
	CHECK(modPriority({}).empty());

	// Texture package of a mod: above the mod's own loose files and archives from HD on (the tier only sets the
	// mip offset), each mod as a block; a package archive with anything but textures and its manifest is not used.
	auto packageA = std::make_shared<const std::vector<unsigned char>>(makeBigWithData({
		{"texturpaket.txt", "TEXTURPAKET 1 UHD"}, {"Art\\Textures\\t.dds", "mod1 paket"}}));
	auto packageB = std::make_shared<const std::vector<unsigned char>>(makeBigWithData({
		{"texturpaket.txt", "TEXTURPAKET 1 UHD"}, {"Art\\Textures\\u.dds", "mod1 paket b"}, {"Art\\Textures\\t.dds", "mod1 paket b"}}));
	auto packageBad = std::make_shared<const std::vector<unsigned char>>(makeBigWithData({
		{"Art\\Textures\\v.dds", "bad"}, {"Data\\INI\\x.ini", "bad"}}));
	auto mod2 = std::make_shared<const std::vector<unsigned char>>(makeBigWithData({
		{"Art\\Textures\\t.dds", "mod2"}, {"Art\\Textures\\w.dds", "mod2"}}));
	auto base2 = std::make_shared<const std::vector<unsigned char>>(makeBigWithData({
		{"Art\\Textures\\t.dds", "base"}, {"Art\\Textures\\u.dds", "base"}, {"Art\\Textures\\v.dds", "base"},
		{"Art\\Textures\\w.dds", "base"}}));
	std::map<std::string, std::unique_ptr<Source>> modSources;
	modSources["M1\\Texturstufen\\Paket\\a.big"] = BigSource::open("M1\\Texturstufen\\Paket\\a.big", readAtMemory(packageA), packageA->size());
	modSources["M1\\Texturstufen\\Paket\\b.big"] = BigSource::open("M1\\Texturstufen\\Paket\\b.big", readAtMemory(packageB), packageB->size());
	modSources["M1\\Texturstufen\\Paket\\z.big"] = BigSource::open("M1\\Texturstufen\\Paket\\z.big", readAtMemory(packageBad), packageBad->size());
	modSources["M2.big"] = BigSource::open("M2.big", readAtMemory(mod2), mod2->size());
	modSources["Base.big"] = BigSource::open("Base.big", readAtMemory(base2), base2->size());
	ModInput packageMod;
	packageMod.folder = "M1\\";
	packageMod.packageArchives = {"M1\\Texturstufen\\Paket\\z.big", "M1\\Texturstufen\\Paket\\b.big", "M1\\Texturstufen\\Paket\\a.big"};
	ModInput archive2;
	archive2.archive = "M2.big";
	for (int tier = 0; tier <= 2; ++tier)
	{
		MountInputs in;
		in.baseArchives = {"Base.big"};
		in.tier = tier;
		in.mods = {packageMod, archive2};	// a folder and an archive: the folder with its package on top
		std::vector<Layer> layers;
		for (const Mount &mount : buildMountPlan(in))
			layers.push_back({modSources[mount.path].get(), mount.texturesOnly});
		std::vector<std::string> rejected;
		Index index;
		index.build(layers, &rejected);
		auto from = [&index](const char *path) -> std::string
		{
			const Hit *hit = index.find(path);
			return hit == nullptr ? std::string("-") : readAll(*hit->source, *hit->entry);
		};
		// the archives of a package load in name order with overwrite: b above a
		CHECK(from("art/textures/t.dds") == (tier >= 1 ? "mod1 paket b" : "mod2"));
		CHECK(from("art/textures/w.dds") == "mod2");
		CHECK(from("art/textures/u.dds") == (tier >= 1 ? "mod1 paket b" : "base"));
		CHECK(from("art/textures/v.dds") == "base");
		CHECK(from("texturpaket.txt") == (tier >= 1 ? "TEXTURPAKET 1 UHD" : "-"));
		CHECK(rejected.size() == (tier >= 1 ? 1u : 0u));
	}
}

static void testTextFiles()
{
	using namespace Engine;
	std::printf("TextFiles\n");
	int version = 0, language = 0;
	std::vector<CsfEntry> csf;

	std::vector<unsigned char> good = makeCsf({{"GUI:Ok", u"OK", ""}, {"Map:Intro", u"Hallo \x00e4", "intro01"}}, 2);
	CHECK(readCsf(good.data(), good.size(), version, language, csf) == CsfError::None);
	CHECK(version == 3 && language == 7 && csf.size() == 2);
	CHECK(csf.size() == 2 && csf[1].label == "Map:Intro" && csf[1].text == u"Hallo \x00e4" && csf[1].speech == "intro01");

	CHECK(readCsf(good.data(), 10, version, language, csf) == CsfError::TruncatedHeader);
	std::vector<unsigned char> bad = good;
	bad[0] = 'X';
	CHECK(readCsf(bad.data(), bad.size(), version, language, csf) == CsfError::NotCsf);
	// more labels than announced: the original wrote past its table
	std::vector<unsigned char> more = makeCsf({{"a", u"x", ""}, {"b", u"y", ""}}, 1);
	CHECK(readCsf(more.data(), more.size(), version, language, csf) == CsfError::BadLabelCount);
	std::vector<unsigned char> huge = makeCsf({}, 0x40000000);
	CHECK(readCsf(huge.data(), huge.size(), version, language, csf) == CsfError::BadLabelCount);
	CHECK(readCsf(good.data(), good.size() - 3, version, language, csf) == CsfError::Truncated);
	std::vector<unsigned char> longLabel = makeCsf({{std::string(TEXT_FILE_MAX_LABEL + 1, 'L'), u"x", ""}}, 1);
	CHECK(readCsf(longLabel.data(), longLabel.size(), version, language, csf) == CsfError::TooLong);

	std::vector<StrEntry> str;
	const char *text =
		"// comment\r\n"
		"GUI:Ok\r\n"
		"\"Ok \\\"quoted\\\"\r\n"
		"  second line\" = speech_12\r\n"
		"END\r\n"
		"\n"
		"Map:Two\n"
		"\"one\"\n"
		"\"two\"\n"
		"end\n";
	CHECK(readStr(text, std::strlen(text), str));
	CHECK(str.size() == 2);
	CHECK(str.size() == 2 && str[0].label == "GUI:Ok" && str[0].text == "Ok \\\"quoted\\\"   second line" && str[0].speech == "speech_12e");
	CHECK(str.size() == 2 && str[1].text == "one" && str[1].extraTexts == 1);

	// the file ends inside the quote and inside the label: no endless loop, the label is kept
	const char *cut = "Label\n\"never closed";
	CHECK(!readStr(cut, std::strlen(cut), str));
	CHECK(str.size() == 1 && str[0].label == "Label" && str[0].text == "never closed ");

	// a speech name longer than any buffer is cut, not written past it
	std::string longSpeech = "L\n\"t\" = " + std::string(100000, 'w') + "\nEND\n";
	CHECK(readStr(longSpeech.data(), longSpeech.size(), str));
	CHECK(str.size() == 1 && str[0].speech.size() < TEXT_FILE_MAX_LABEL);
}

// FONT fields as the .wnd parser hands them over (whitespace already turned into spaces by
// readUntilSemicolon). The name once came out empty because the search for the quote stopped at the
// NUL that strtok writes behind the label.
// WWFormat::Into must give what snprintf gave for the formats it replaced.
static void testWWFormat()
{
	std::printf("WWFormat\n");
#ifdef __cpp_lib_format
	char a[64], b[64];
#define SAME(PRINTF_FMT, STD_FMT, ...) \
	do { \
		std::snprintf(a, sizeof(a), PRINTF_FMT, __VA_ARGS__); \
		WWFormat::Into(b, STD_FMT, __VA_ARGS__); \
		CHECK(std::strcmp(a, b) == 0); \
	} while (0)
	SAME("%s%02d", "{}{:02d}", "Bone", 7);
	SAME("%s%02d", "{}{:02d}", "Bone", 123);
	SAME("%d %u %x %X %o", "{:d} {:d} {:x} {:X} {:o}", -5, 7u, 255, 255, 8);
	SAME("%08X", "{:08X}", 0xBEEFu);
	SAME("%.2f %.1f %f", "{:.2f} {:.1f} {:f}", 3.14159, 2.55, 1.5);
	SAME("%g %g %g", "{:g} {:g} {:g}", 0.5, 100000.0, 1e-5);
	SAME("%g", "{:g}", 3.0f);
	SAME("%5d|%-5d|%+d", "{:5d}|{:<5d}|{:+d}", 42, 42, 42);
	SAME("%8s|%-8s|%.3s", "{:>8}|{:<8}|{:.3}", "abc", "abc", "abcdef");
	SAME("%.3d", "{:03d}", 5);
	SAME("%c%c", "{:c}{:c}", 'o', 'k');
	SAME("%d", "{:d}", true);
#undef SAME
	WWFormat::Into(b, "100%");
	CHECK(std::strcmp(b, "100%") == 0);
	std::snprintf(a, sizeof(a), "%.*s.tga", 6, "sprite_shadow");
	WWFormat::Into(b, "{0:.{1}}.tga", "sprite_shadow", 6);
	CHECK(std::strcmp(a, b) == 0);
	// The INI writers print floats with %.2f / %2.2f: identical text for arbitrary float values.
	unsigned rng = 12345u;
	int floatMismatches = 0;
	for (int i = 0; i < 200000; ++i) {
		rng = rng * 1664525u + 1013904223u;
		const unsigned bits = (i % 2) ? rng : ((rng >> 8) | 0x3F000000u) ;
		float f;
		std::memcpy(&f, &bits, sizeof(f));
		if (!(f == f) || f > 1e30f || f < -1e30f) continue;
		char c[400], d[400];
		std::snprintf(c, sizeof(c), "%.2f", f);
		WWFormat::Into(d, "{:.2f}", f);
		if (std::strcmp(c, d) != 0) ++floatMismatches;
		std::snprintf(c, sizeof(c), "%2.2f", f);
		WWFormat::Into(d, "{:2.2f}", f);
		if (std::strcmp(c, d) != 0) ++floatMismatches;
	}
	CHECK(floatMismatches == 0);
	// truncation like snprintf: terminated, length of the full result returned
	char small[8];
	const int full = WWFormat::Into(small, "{}", "0123456789");
	CHECK(full == 10);
	CHECK(std::strcmp(small, "0123456") == 0);
	const int sized = WWFormat::Into(small, 4, "{}", "abcdef");
	CHECK(sized == 6);
	CHECK(std::strcmp(small, "abc") == 0);
#endif
}

// Engine::formatInto and the texts of the Code/Core/EngineCore building blocks must read as the printf
// formats they replaced.
// Engine::CheckedWideFormat: the rules of the Microsoft wide functions, checked while compiling.
static void testWideFormatCheck()
{
	std::printf("WideFormatCheck\n");
	using Engine::WideFormat::checkFor;
	using R = Engine::WideFormat::Result;
	static_assert(checkFor<>(L"plain text, 100%% sure") == R::Ok);
	static_assert(checkFor<int, const wchar_t *>(L"%d: %s") == R::Ok);
	static_assert(checkFor<const char *, const char *>(L"%hs %S") == R::Ok);
	static_assert(checkFor<const wchar_t *>(L"%ls") == R::Ok);
	static_assert(checkFor<double, float>(L"%.2f %g") == R::Ok);
	static_assert(checkFor<int, double>(L"%*.1f") == R::Ok);
	static_assert(checkFor<unsigned, long long, unsigned long long>(L"%08X %lld %I64u") == R::Ok);
	static_assert(checkFor<const char *>(L"%s") == R::WrongType);	// %s is a wide string here
	static_assert(checkFor<const wchar_t *>(L"%hs") == R::WrongType);
	static_assert(checkFor<int>(L"%f") == R::WrongType);
	static_assert(checkFor<long long>(L"%d") == R::WrongType);	// 64 bits need ll or I64
	static_assert(checkFor<int>(L"%d %d") == R::TooFewArguments);
	static_assert(checkFor<int, int>(L"%d") == R::TooManyArguments);
	static_assert(checkFor<int *>(L"%n") == R::UnknownConversion);
	Engine::CheckedWideFormat<int, const wchar_t *> checked(L"%d %s");
	CHECK(checked.str[0] == L'%');
}

static void testEngineFormatInto()
{
	std::printf("Engine::formatInto\n");
	char a[400], b[400];
#define SAME(PRINTF_FMT, STD_FMT, ...) \
	do { \
		std::snprintf(a, sizeof(a), PRINTF_FMT, __VA_ARGS__); \
		Engine::formatInto(b, STD_FMT, __VA_ARGS__); \
		CHECK(std::strcmp(a, b) == 0); \
	} while (0)
	// log lines (EngineLog.cpp), including texts longer than the column
	SAME("[%8u] %-5s %-6s %-11s %s\n", "[{:>8}] {:<5} {:<6} {:<11} {}\n", 1234u, "WARN", "NET", "main+sim", "text");
	SAME("[%8u] %-5s %-6s %-11s %s\n", "[{:>8}] {:<5} {:<6} {:<11} {}\n", 123456789u, "ERROR", "SCRIPTS", "worker-long-name", "x");
	std::snprintf(a, sizeof(a), "[%8u] %-5s %-6s %-11s (tid %u) %.*s\n", 5u, "INFO", "ENGINE", "main", 77u, 3, "abcdef");
	Engine::formatInto(b, "[{:>8}] {:<5} {:<6} {:<11} (tid {}) {}\n", 5u, "INFO", "ENGINE", "main", 77u,
		std::string_view("abcdef", 3));
	CHECK(std::strcmp(a, b) == 0);
	SAME("%s(%d) %s: %s", "{}({}) {}: {}", "file.cpp", -3, "fn", "msg");
	// CpuTopology::describe and VideoMemoryInfo::describe
	SAME("%d cores (%d P, %d E), %d logical processors, SMT %s, hybrid %s, NUMA nodes %d, recommended workers %d",
		"{} cores ({} P, {} E), {} logical processors, SMT {}, hybrid {}, NUMA nodes {}, recommended workers {}",
		8, 6, 2, 16, "yes", "no", 1, 7);
	SAME("%s (%04X:%04X), UMA %s, budget %s%llu MB", "{} ({:04X}:{:04X}), UMA {}, budget {}{} MB", "Adapter", 0x8086u, 0x412u, "no",
		"unknown/", 4095ull);
	// tables of LogicAccessGuard.cpp and ThreadRole.cpp, also with fields wider than the column
	SAME("%-12s %-40s %12llu  %s\n", "{:<12} {:<40} {:>12}  {}\n", "Category", "Function", 12ull, "a.cpp:1");
	SAME("%-10llu %-6s %-6s %-44s %s:%d\n", "{:<10} {:<6} {:<6} {:<44} {}:{}\n", 3ull, "main", "worker",
		"aVeryLongFunctionNameThatIsWiderThanTheFortyFourCharacterColumn", "b.cpp", 9);
	// network texts (NetAddress.cpp, Wire.cpp, PortMapping.cpp, InviteCode.cpp)
	const std::uint8_t ip[4] = { 192, 168, 0, 255 };
	SAME("%u.%u.%u.%u:%u", "{}.{}.{}.{}:{}", ip[0], ip[1], ip[2], ip[3], static_cast<std::uint16_t>(65535));
	SAME("%x:%x", "{:x}:{:x}", static_cast<std::uint16_t>(0xdb8), static_cast<std::uint16_t>(0));
#undef SAME
	Engine::WireEndpoint v4 = Engine::WireEndpoint::ipv4(0xC0A80001u, 8088);
	CHECK(v4.text() == "192.168.0.1:8088");
	Engine::WireEndpoint v6;
	v6.family = 6;
	v6.address[0] = 0x20; v6.address[1] = 0x01; v6.address[2] = 0x0d; v6.address[3] = 0xb8; v6.address[15] = 0x02;
	v6.port = 8086;
	CHECK(v6.text() == "[2001:db8:0:0:0:0:0:2]:8086");
	CHECK(Engine::WireEndpoint().text() == "-");
	CHECK(Engine::formatInviteCode("ABCD", "0123ABCD", "example.org", 12345) == "ABCD-0123ABCD@example.org:12345");
	CHECK(Engine::PortMapping::ipv4Text(0x0A000102u) == "10.0.1.2");
	// truncation like snprintf: terminated, length of the full result returned
	char small[8];
	CHECK(Engine::formatInto(small, "{}", "0123456789") == 10);
	CHECK(std::strcmp(small, "0123456") == 0);
	CHECK(Engine::formatInto(small, 4, "{}", "abcdef") == 6);
	CHECK(std::strcmp(small, "abc") == 0);
	CHECK(Engine::formatInto(nullptr, 0, "{}", 12345) == 5);
}

// Engine::ScopedGlobal swaps a global pointer for a scope and restores it, also on an exception.
static void testScopedGlobal()
{
	std::printf("ScopedGlobal\n");
	struct Service { int value; };
	Service real{ 1 }, fake{ 2 }, inner{ 3 };
	Service *global = &real;
	{
		Engine::ScopedGlobal<Service> guard(global, &fake);
		CHECK(global == &fake);
		CHECK(guard.previous() == &real);
		{
			Engine::ScopedGlobal<Service> nested(global, &inner);
			CHECK(global->value == 3);
		}
		CHECK(global == &fake);
	}
	CHECK(global == &real);
	try {
		Engine::ScopedGlobal<Service> guard(global, nullptr);
		CHECK(global == nullptr);
		throw std::runtime_error("test");
	} catch (const std::runtime_error &) {
	}
	CHECK(global == &real);
}

// Engine::ScopedContext gives a test its own set of singletons and restores the real ones afterwards.
static void testEngineContext()
{
	std::printf("EngineContext\n");
	// The macro TheGameLogic would expand inside member accesses, so the test names the member through the context.
#pragma push_macro("TheGameLogic")
#undef TheGameLogic
	GameLogic *fake = reinterpret_cast<GameLogic *>(static_cast<std::uintptr_t>(0x1000));
	Engine::context().gameLogic = fake;
	CHECK(Engine::g_defaultContext.gameLogic == fake);
	{
		Engine::EngineContext own;
		Engine::ScopedContext scope(own);
		CHECK(Engine::context().gameLogic == nullptr);
		Engine::context().gameLogic = reinterpret_cast<GameLogic *>(static_cast<std::uintptr_t>(0x2000));
		CHECK(own.gameLogic != nullptr);
		CHECK(Engine::g_defaultContext.gameLogic == fake);
	}
	CHECK(Engine::context().gameLogic == fake);
	Engine::context().gameLogic = nullptr;
#pragma pop_macro("TheGameLogic")
}

static void testWndFont()
{
	using namespace Engine;
	std::printf("WndFields\n");
	std::string name;
	int size = -1, bold = -1;

	char plain[] = "FONT = NAME: \"Arial\", SIZE: 14, BOLD: 1";
	CHECK(parseWndFont(plain, name, size, bold));
	CHECK(name == "Arial" && size == 14 && bold == 1);

	char spaced[] = "FONT = NAME: \"Times New Roman\", SIZE: 8, BOLD: 0";
	CHECK(parseWndFont(spaced, name, size, bold));
	CHECK(name == "Times New Roman" && size == 8 && bold == 0);

	char tabs[] = "FONT\t=\tNAME:\t\"Generals\",\r\nSIZE:\t12,\tBOLD:\t0";
	CHECK(parseWndFont(tabs, name, size, bold));
	CHECK(name == "Generals" && size == 12 && bold == 0);

	char noBold[] = "FONT = NAME: \"Arial\", SIZE: 10";
	CHECK(parseWndFont(noBold, name, size, bold));
	CHECK(name == "Arial" && size == 10 && bold == 0);

	// damaged fields: no quote, no value, empty text -- empty name, no read past the text
	char noQuote[] = "FONT = NAME: Arial, SIZE: 14, BOLD: 1";
	CHECK(!parseWndFont(noQuote, name, size, bold));
	CHECK(name.empty() && size == 0 && bold == 0);
	char onlyLabel[] = "FONT";
	CHECK(!parseWndFont(onlyLabel, name, size, bold));
	char empty[] = "";
	CHECK(!parseWndFont(empty, name, size, bold));
	char openQuote[] = "FONT = NAME: \"";
	CHECK(parseWndFont(openQuote, name, size, bold));
	CHECK(name.empty());
}

// A memory source for readWndChunk.
struct WndMemSource
{
	const char *p;
	std::size_t left;
};

static bool wndMemByte(void *context, char &byte)
{
	WndMemSource *s = static_cast<WndMemSource *>(context);
	if (s->left == 0)
		return false;
	byte = *s->p++;
	--s->left;
	return true;
}

static std::string wndChunk(WndMemSource &src, int maxLen, bool *fits = nullptr)
{
	std::vector<char> buffer(static_cast<std::size_t>(maxLen) + 8, 'x');
	const bool ok = Engine::readWndChunk(wndMemByte, &src, buffer.data(), maxLen);
	if (fits)
		*fits = ok;
	return std::string(buffer.data());
}

// Chunks of a layout file: `;` ends a chunk, whitespace becomes spaces, the end of the data ends the last one.
static void testWndChunk()
{
	using namespace Engine;
	std::printf("WndChunk\n");

	const char *text = "  FONT = NAME: \"Arial\", SIZE: 14;\r\n\tTEXT = \"x\" ;last";
	WndMemSource src = { text, std::strlen(text) };
	bool fits = false;
	CHECK(wndChunk(src, 200, &fits) == "FONT = NAME: \"Arial\", SIZE: 14" && fits);
	CHECK(wndChunk(src, 200, &fits) == "TEXT = \"x\" " && fits);
	CHECK(wndChunk(src, 200, &fits) == "last" && fits);		// no closing ';' at the end of the file
	CHECK(wndChunk(src, 200, &fits) == "" && fits);			// nothing left

	WndMemSource ws = { "A\t\nB;", 5 };
	CHECK(wndChunk(ws, 50) == "A  B");						// every whitespace character after the start becomes one space

	WndMemSource only = { " \t ;", 4 };
	CHECK(wndChunk(only, 50) == "");
	WndMemSource none = { "", 0 };
	CHECK(wndChunk(none, 50, &fits) == "" && fits);

	// too long: cut, terminated, reported
	WndMemSource longer = { "ABCDEFGHIJ;", 11 };
	CHECK(wndChunk(longer, 8, &fits) == "ABCDEFG" && !fits);
	WndMemSource exact = { "ABCDEFG;", 8 };
	CHECK(wndChunk(exact, 8, &fits) == "ABCDEFG" && fits);	// seven characters and the ';' fit into 8
	WndMemSource tiny = { "AB;", 3 };
	CHECK(wndChunk(tiny, 1, &fits) == "" && !fits);

	// bytes above 127 (CP1252 text) are no whitespace and must not upset isspace
	const char high[] = "\xe4\xf6;";
	WndMemSource hi = { high, 3 };
	CHECK(wndChunk(hi, 50, &fits) == "\xe4\xf6" && fits);
}

// Values in quotes and the position behind the first quote.
static void testWndQuotedValue()
{
	using namespace Engine;
	std::printf("WndQuotedValue\n");

	char name[] = "NAME = \"MainMenu.wnd:Button\"";
	CHECK(std::string(wndQuotedValue(name, "\"")) == "MainMenu.wnd:Button");
	char text[] = "TEXT = \"one, two\tthree\" tail";
	CHECK(std::string(wndQuotedValue(text, "\n\r\t\"")) == "one, two");		// cut at the first separator, as EA's strtok did

	// nothing usable: empty, never null, no read past the end
	char noQuote[] = "NAME = Foo";
	CHECK(std::string(wndQuotedValue(noQuote, "\"")).empty());
	char empty[] = "";
	CHECK(std::string(wndQuotedValue(empty, "\"")).empty());
	char lone[] = "\"";
	CHECK(std::string(wndQuotedValue(lone, "\"")).empty());
	char twoQuotes[] = "NAME = \"\"";
	CHECK(std::string(wndQuotedValue(twoQuotes, "\"")).empty());
	char open[] = "NAME = \"unterminated";
	CHECK(std::string(wndQuotedValue(open, "\"")) == "unterminated");

	char a[] = "FOO \"bar\"";
	CHECK(wndAfterFirstQuote(a) == a + 5);
	char b[] = "no quote";
	CHECK(wndAfterFirstQuote(b) == b + 8 && *wndAfterFirstQuote(b) == '\0');
	char c[] = "";
	CHECK(wndAfterFirstQuote(c) == c);
}

// Numbers: a missing or unparsable value reads as 0, never crashes.
static void testWndNumbers()
{
	using namespace Engine;
	std::printf("WndNumbers\n");

	int i = 7;
	CHECK(wndScanInt("42", i) == 1 && i == 42);
	CHECK(wndScanInt("-5", i) == 1 && i == -5);
	CHECK(wndScanInt(nullptr, i) == 0 && i == -5);			// unchanged
	i = 7;
	CHECK(wndScanInt("abc", i) == 0 && i == 7);				// unchanged
	unsigned int u = 9;
	CHECK(wndScanUnsigned("255", u) == 1 && u == 255);
	CHECK(wndScanUnsigned(nullptr, u) == 0 && u == 255);
	bool b = true;
	CHECK(wndScanBool("0", b) == 1 && !b);
	CHECK(wndScanBool("2", b) == 1 && b);
	CHECK(wndScanBool(nullptr, b) == 0 && !b);				// reads as 0
	b = true;
	CHECK(wndScanBool("x", b) == 0 && !b);
	short s = 5;
	CHECK(wndScanShort("300", s) == 1 && s == 300);
	CHECK(wndScanShort(nullptr, s) == 0 && s == 0);

	CHECK(wndToInt("12") == 12 && wndToInt("  9") == 9 && wndToInt("-3") == -3);
	CHECK(wndToInt(nullptr) == 0 && wndToInt("") == 0 && wndToInt("abc") == 0);
}

// A counted list of flags (the pane flags of a tab control).
static void testWndBoolList()
{
	using namespace Engine;
	std::printf("WndBoolList\n");
	const char *seps = " :,\n\r\t";

	// the label and the count come first, as in the parser
	char full[] = "PANEDISABLED: 3 1 0 1";
	bool out[8] = {};
	std::strtok(full, seps);
	std::strtok(nullptr, seps);
	CHECK(wndReadBoolList(3, seps, out, 8) == 3);
	CHECK(out[0] && !out[1] && out[2] && !out[3]);

	// fewer values than announced: the list ends, nothing is invented
	char shortList[] = "PANEDISABLED: 5 1 1";
	bool out2[8] = {};
	std::strtok(shortList, seps);
	std::strtok(nullptr, seps);
	CHECK(wndReadBoolList(5, seps, out2, 8) == 2);
	CHECK(out2[0] && out2[1] && !out2[2]);

	// more values than room: read and dropped, no write behind the array
	char many[] = "PANEDISABLED: 6 1 1 1 1 1 1";
	bool guard[4] = { false, false, false, false };
	std::strtok(many, seps);
	std::strtok(nullptr, seps);
	CHECK(wndReadBoolList(6, seps, guard, 2) == 6);
	CHECK(guard[0] && guard[1] && !guard[2] && !guard[3]);

	char none[] = "PANEDISABLED: 0";
	std::strtok(none, seps);
	std::strtok(nullptr, seps);
	CHECK(wndReadBoolList(0, seps, out, 8) == 0);
	CHECK(wndReadBoolList(-4, seps, out, 8) == 0);
}

// Limits and the check of a data field against the window type.
static void testWndLimits()
{
	using namespace Engine;
	std::printf("WndLimits\n");

	CHECK(wndClamp(5, 1, 10) == 5 && wndClamp(-3, 1, 10) == 1 && wndClamp(99, 1, 10) == 10);
	CHECK(wndClamp(1, 1, 10) == 1 && wndClamp(10, 1, 10) == 10);
	CHECK(wndClamp(2147483647, 0, 65535) == 65535 && wndClamp(-2147483647 - 1, 0, 65535) == 0);

	CHECK(wndFieldFitsType("LISTBOXDATA", "SCROLLLISTBOX"));
	CHECK(!wndFieldFitsType("LISTBOXDATA", "STATICTEXT"));		// would write list data into a text data block
	CHECK(!wndFieldFitsType("LISTBOXDATA", "PUSHBUTTON"));		// a window without data block
	CHECK(wndFieldFitsType("SLIDERDATA", "VERTSLIDER") && wndFieldFitsType("SLIDERDATA", "HORZSLIDER"));
	CHECK(!wndFieldFitsType("SLIDERDATA", "SCROLLLISTBOX"));
	CHECK(wndFieldFitsType("COMBOBOXDATA", "COMBOBOX") && !wndFieldFitsType("COMBOBOXDATA", "ENTRYFIELD"));
	CHECK(wndFieldFitsType("RADIOBUTTONDATA", "RADIOBUTTON") && !wndFieldFitsType("RADIOBUTTONDATA", "COMBOBOX"));
	CHECK(wndFieldFitsType("STATICTEXTDATA", "STATICTEXT") && !wndFieldFitsType("STATICTEXTDATA", "ENTRYFIELD"));
	CHECK(wndFieldFitsType("TEXTENTRYDATA", "ENTRYFIELD") && !wndFieldFitsType("TEXTENTRYDATA", "STATICTEXT"));
	CHECK(wndFieldFitsType("TABCONTROLDATA", "TABCONTROL") && !wndFieldFitsType("TABCONTROLDATA", "PUSHBUTTON"));

	// fields without own data fit every window
	CHECK(wndFieldFitsType("NAME", "PUSHBUTTON") && wndFieldFitsType("TEXT", "STATICTEXT") && wndFieldFitsType("ENABLEDDRAWDATA", "USER"));
	CHECK(!wndFieldFitsType(nullptr, "USER") && !wndFieldFitsType("NAME", nullptr));
	CHECK(!wndFieldFitsType("LISTBOXDATA", ""));
}

// ------------------------------------------------------------------ NetEmulator

static void testNetEmulator()
{
	std::printf("NetEmulator\n");
	using Engine::NetEmulator;
	using Engine::NetEmulatorSettings;

	// Inactive settings: delivered at once, in order, bytes and address unchanged.
	{
		NetEmulator emu;
		emu.configure(NetEmulatorSettings());
		CHECK(!emu.active());
		const std::uint8_t a[3] = { 1, 2, 3 };
		const std::uint8_t b[2] = { 9, 8 };
		CHECK(emu.push(a, sizeof(a), 0x7F000002u, 8088, 1000) == 1);
		CHECK(emu.push(b, sizeof(b), 0x7F000003u, 8086, 1000) == 1);
		NetEmulator::Packet p;
		CHECK(emu.pop(1000, p) && p.data.size() == 3 && p.data[2] == 3 && p.addr == 0x7F000002u && p.port == 8088);
		CHECK(emu.pop(1000, p) && p.data.size() == 2 && p.data[0] == 9 && p.port == 8086);
		CHECK(!emu.pop(1000, p));
	}

	// Fixed latency: nothing before the delay, then in arrival order.
	{
		NetEmulator emu;
		NetEmulatorSettings s;
		s.latencyMs = 100;
		emu.configure(s);
		CHECK(emu.active());
		for (std::uint8_t i = 0; i < 5; ++i)
			emu.push(&i, 1, 1, 1, 50 + i);
		NetEmulator::Packet p;
		CHECK(!emu.pop(149, p));
		CHECK(emu.pop(150, p) && p.data[0] == 0);
		CHECK(!emu.pop(150, p));
		CHECK(emu.pop(1000, p) && p.data[0] == 1);
		CHECK(emu.pop(1000, p) && p.data[0] == 2);
		CHECK(emu.queued() == 2);
	}

	// Clock wrap-around.
	{
		NetEmulator emu;
		NetEmulatorSettings s;
		s.latencyMs = 20;
		emu.configure(s);
		const std::uint8_t x = 7;
		emu.push(&x, 1, 1, 1, 0xFFFFFFF0u);
		NetEmulator::Packet p;
		CHECK(!emu.pop(0xFFFFFFFFu, p));
		CHECK(emu.pop(4u, p) && p.data[0] == 7);
	}

	// Loss, duplication and reordering: rates close to the setting, same seed same decisions.
	{
		NetEmulatorSettings s;
		s.lossPercent = 20;
		s.duplicatePercent = 10;
		s.reorderPercent = 5;
		s.jitterMs = 10;
		s.seed = 1234;
		NetEmulator a;
		NetEmulator b;
		a.configure(s);
		b.configure(s);
		const int N = 20000;
		std::vector<std::uint32_t> outA;
		std::vector<std::uint32_t> outB;
		NetEmulator::Packet p;
		for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(N); ++i)
		{
			a.push(&i, sizeof(i), 1, 1, i);
			b.push(&i, sizeof(i), 1, 1, i);
			while (a.pop(i, p)) { std::uint32_t v; std::memcpy(&v, p.data.data(), sizeof(v)); outA.push_back(v); }
			while (b.pop(i, p)) { std::uint32_t v; std::memcpy(&v, p.data.data(), sizeof(v)); outB.push_back(v); }
		}
		while (a.pop(N + 100000, p)) { std::uint32_t v; std::memcpy(&v, p.data.data(), sizeof(v)); outA.push_back(v); }
		while (b.pop(N + 100000, p)) { std::uint32_t v; std::memcpy(&v, p.data.data(), sizeof(v)); outB.push_back(v); }
		CHECK(outA == outB);
		const Engine::NetEmulatorStats &st = a.stats();
		CHECK(st.received == static_cast<std::uint64_t>(N));
		CHECK(st.dropped > N * 17 / 100 && st.dropped < N * 23 / 100);
		CHECK(st.duplicated > (N - st.dropped) * 8 / 100 && st.duplicated < (N - st.dropped) * 12 / 100);
		CHECK(st.reordered > (N - st.dropped) * 3 / 100 && st.reordered < (N - st.dropped) * 7 / 100);
		CHECK(st.delivered == st.received - st.dropped + st.duplicated);
		CHECK(outA.size() == st.delivered);
		int inversions = 0;
		for (std::size_t i = 1; i < outA.size(); ++i)
			if (outA[i] < outA[i - 1])
				++inversions;
		CHECK(inversions > 0);

		NetEmulatorSettings other = s;
		other.seed = 4321;
		NetEmulator c;
		c.configure(other);
		for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(N); ++i)
			c.push(&i, sizeof(i), 1, 1, i);
		CHECK(c.stats().dropped != st.dropped || c.stats().duplicated != st.duplicated);
	}

	// Clamping and queue bound.
	{
		NetEmulator emu;
		NetEmulatorSettings s;
		s.lossPercent = 500;
		s.latencyMs = 1000000;
		emu.configure(s);
		CHECK(emu.settings().lossPercent == 100 && emu.settings().latencyMs == 10000);
		const std::uint8_t x = 0;
		CHECK(emu.push(&x, 1, 1, 1, 0) == 0);

		s.lossPercent = 0;
		s.latencyMs = 5000;
		emu.configure(s);
		for (std::size_t i = 0; i < NetEmulator::MAX_QUEUED + 10; ++i)
			emu.push(&x, 1, 1, 1, 0);
		CHECK(emu.queued() == NetEmulator::MAX_QUEUED);
		CHECK(emu.stats().overflowed == 10);
		emu.configure(s);
		CHECK(emu.queued() == 0);
	}
}

// ------------------------------------------------------------------ PacketSeal

static void fromHex(const char *hex, std::uint8_t *out, std::size_t size)
{
	for (std::size_t i = 0; i < size; ++i)
	{
		unsigned v = 0;
		std::sscanf(hex + 2 * i, "%2x", &v);
		out[i] = static_cast<std::uint8_t>(v);
	}
}

static std::string toHex(const std::uint8_t *p, std::size_t size)
{
	std::string s;
	char b[3];
	for (std::size_t i = 0; i < size; ++i)
	{
		std::snprintf(b, sizeof(b), "%02x", p[i]);
		s += b;
	}
	return s;
}

static void testPacketSeal()
{
	std::printf("PacketSeal\n");
	using namespace Engine;

	// Monocypher as built here against published vectors: X25519 (RFC 7748 6.1), BLAKE2b-512("abc").
	SealKeyPair alice;
	SealKeyPair bob;
	fromHex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice.secretKey, 32);
	fromHex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bob.secretKey, 32);
	crypto_x25519_public_key(alice.publicKey, alice.secretKey);
	crypto_x25519_public_key(bob.publicKey, bob.secretKey);
	CHECK(toHex(alice.publicKey, 32) == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
	CHECK(toHex(bob.publicKey, 32) == "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
	SealKeyPair again;
	again.fromSecret(alice.secretKey);
	CHECK(std::memcmp(again.publicKey, alice.publicKey, 32) == 0);	// the same keys every run (test harnesses)
	std::uint8_t shared[32];
	crypto_x25519(shared, alice.secretKey, bob.publicKey);
	CHECK(toHex(shared, 32) == "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
	std::uint8_t hash[64];
	crypto_blake2b(hash, 64, reinterpret_cast<const std::uint8_t *>("abc"), 3);
	CHECK(toHex(hash, 64) == "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
		"7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923");

	// Session keys: A's send key is B's receive key and the other way round; the context matters.
	const char ctx[] = "game 1234, slots 0 and 1";
	SealSessionKeys ka;
	SealSessionKeys kb;
	CHECK(deriveSessionKeys(alice, bob.publicKey, ctx, sizeof(ctx), ka));
	CHECK(deriveSessionKeys(bob, alice.publicKey, ctx, sizeof(ctx), kb));
	CHECK(std::memcmp(ka.send, kb.receive, SEAL_KEY_SIZE) == 0);
	CHECK(std::memcmp(ka.receive, kb.send, SEAL_KEY_SIZE) == 0);
	CHECK(std::memcmp(ka.send, ka.receive, SEAL_KEY_SIZE) != 0);
	// Pins the key derivation: a change here changes the protocol (bump SEAL_MAGIC).
	CHECK(toHex(ka.send, 32) == "7813e99d6cfb7a603bfbdc7441eea54ec0948fafc428aeb884597512ecf6aad8");
	SealSessionKeys other;
	CHECK(deriveSessionKeys(alice, bob.publicKey, "x", 1, other));
	CHECK(std::memcmp(other.send, ka.send, SEAL_KEY_SIZE) != 0);
	// Low-order peer key (all zero) gives no secret.
	const std::uint8_t zeroKey[32] = {};
	CHECK(!deriveSessionKeys(alice, zeroKey, ctx, sizeof(ctx), other));

	// Random key pairs differ and work together.
	SealKeyPair r1;
	SealKeyPair r2;
	CHECK(r1.generate() && r2.generate());
	CHECK(std::memcmp(r1.publicKey, r2.publicKey, 32) != 0);

	SealedChannel a;
	SealedChannel b;
	std::uint8_t wire[600];
	std::uint8_t plain[600];
	const char msg[] = "move unit 42 to (100, 200)";
	CHECK(a.seal(msg, sizeof(msg), wire) == 0);	// no keys yet
	a.setKeys(ka);
	b.setKeys(kb);
	const std::size_t n = a.seal(msg, sizeof(msg), wire);
	CHECK(n == sizeof(msg) + SEAL_OVERHEAD);
	CHECK(std::memcmp(wire + SEAL_HEADER_SIZE, msg, sizeof(msg)) != 0);	// encrypted
	CHECK(b.open(wire, n, plain) == static_cast<long>(sizeof(msg)) && std::memcmp(plain, msg, sizeof(msg)) == 0);
	// replayed
	CHECK(b.open(wire, n, plain) == -1 && b.stats().rejectedReplay == 1);
	// every single bit flip is rejected
	int accepted = 0;
	for (std::size_t bit = 0; bit < n * 8; ++bit)
	{
		std::uint8_t copy[600];
		std::memcpy(copy, wire, n);
		copy[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
		SealedChannel fresh;
		fresh.setKeys(kb);
		accepted += fresh.open(copy, n, plain) >= 0;
	}
	CHECK(accepted == 0);
	// wrong direction: A cannot open its own packets
	const std::size_t n2 = a.seal(msg, sizeof(msg), wire);
	CHECK(a.open(wire, n2, plain) == -1);
	// a third party with its own keys cannot forge
	SealedChannel mallory;
	SealSessionKeys km;
	CHECK(deriveSessionKeys(r1, bob.publicKey, ctx, sizeof(ctx), km));
	mallory.setKeys(km);
	std::size_t n3 = 0;
	for (int i = 0; i < 5; ++i)	// ahead of the counters b has seen, so the tag decides
		n3 = mallory.seal(msg, sizeof(msg), wire);
	CHECK(b.open(wire, n3, plain) == -1 && b.stats().rejectedTag == 1);
	// a trial with the keys of another peer: rejected, not counted
	CHECK(b.open(wire, n3, plain, SealedChannel::Rejects::Ignore) == -1 && b.stats().rejectedTag == 1);
	// short and foreign packets
	CHECK(b.open(wire, SEAL_OVERHEAD - 1, plain) == -1);
	std::uint8_t old[64] = {};
	CHECK(b.open(old, sizeof(old), plain) == -1 && b.stats().rejectedFormat >= 2);
	// empty payload, in place
	const std::size_t n4 = a.seal(nullptr, 0, wire);
	CHECK(n4 == SEAL_OVERHEAD && b.open(wire, n4, wire) == 0);

	// Reordering within the window is accepted, each packet once; older than the window is not.
	{
		SealedChannel s;
		SealedChannel r;
		s.setKeys(ka);
		r.setKeys(kb);
		std::vector<std::vector<std::uint8_t>> packets;
		for (int i = 0; i < 100; ++i)
		{
			std::uint8_t buf[64];
			const std::size_t len = s.seal(&i, sizeof(i), buf);
			packets.emplace_back(buf, buf + len);
		}
		bool ok = true;
		for (int i = 99; i >= 36; --i)
			ok = ok && r.open(packets[i].data(), packets[i].size(), plain) == static_cast<long>(sizeof(int));
		CHECK(ok);
		CHECK(r.open(packets[35].data(), packets[35].size(), plain) == -1);	// 64 behind the newest
		CHECK(r.open(packets[50].data(), packets[50].size(), plain) == -1);	// already accepted
		CHECK(r.stats().opened == 64 && r.stats().rejectedReplay == 2);
		// in place with the same buffer as the wire
		std::vector<std::uint8_t> last = packets[99];
		SealedChannel inPlace;
		inPlace.setKeys(kb);
		int value = -1;
		const long got = inPlace.open(last.data(), last.size(), last.data());
		std::memcpy(&value, last.data(), sizeof(value));
		CHECK(got == static_cast<long>(sizeof(int)) && value == 99);
	}
}


// ------------------------------------------------------------------ NetAddress

static void testNetAddress()
{
	std::printf("NetAddress\n");
	using Engine::NetAddress;
	using Engine::NetTarget;

	auto roundTrip = [](const char *in, const char *expected) {
		NetAddress a;
		return Engine::parseNetAddress(in, a) && a.toString() == expected;
	};

	// IPv4, with and without port
	NetAddress a;
	CHECK(Engine::parseNetAddress("192.168.1.20", a) && a.isV4() && a.ipv4() == 0xC0A80114u && a.port == 0);
	CHECK(Engine::parseNetAddress(" 10.0.0.1:8088 ", a) && a.ipv4() == 0x0A000001u && a.port == 8088);
	CHECK(roundTrip("127.0.0.3", "127.0.0.3") && roundTrip("1.2.3.4:65535", "1.2.3.4:65535"));
	CHECK(roundTrip("010.001.000.009", "10.1.0.9"));	// leading zeros are decimal, not octal
	CHECK(NetAddress::fromIPv4(0x7F000002u, 8086).toString() == "127.0.0.2:8086");
	CHECK(Engine::parseNetAddress("127.0.0.5", a) && a.isLoopback() && !a.isUnspecified());
	CHECK(Engine::parseNetAddress("0.0.0.0", a) && a.isUnspecified());
	for (const char *bad : { "", " ", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.4:0", "1.2.3.4:65536", "1.2.3.4:",
		"1..2.3", ".1.2.3", "1.2.3.4 5", "1.2.3.0004", "-1.2.3.4", "1.2.3.4:80:80", "1.2.3.x" })
	{
		CHECK_SILENT(!Engine::parseNetAddress(bad, a));
	}

	// IPv6 (RFC 5952 output)
	CHECK(roundTrip("::1", "::1") && roundTrip("::", "::"));
	CHECK(roundTrip("2001:DB8:0:0:0:0:0:7", "2001:db8::7"));
	CHECK(roundTrip("2001:db8:0:0:1:0:0:1", "2001:db8::1:0:0:1"));		// first of two equal runs
	CHECK(roundTrip("2001:0db8:0000:0001:0001:0001:0001:0001", "2001:db8:0:1:1:1:1:1"));	// a single zero group stays
	CHECK(roundTrip("fe80::1:2", "fe80::1:2") && roundTrip("1::", "1::") && roundTrip("1:2:3:4:5:6:7::", "1:2:3:4:5:6:7:0"));
	CHECK(roundTrip("[2001:db8::7]:8088", "[2001:db8::7]:8088"));
	CHECK(roundTrip("::1.2.3.5", "::102:305"));
	CHECK(Engine::parseNetAddress("::ffff:192.168.0.1", a) && a.isV4() && a.ipv4() == 0xC0A80001u);
	CHECK(Engine::parseNetAddress("[::ffff:c0a8:1]:5", a) && a.isV4() && a.port == 5);
	CHECK(Engine::parseNetAddress("::1", a) && a.isV6() && a.isLoopback() && a.ipv4() == 0);
	for (const char *bad : { ":::", "1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7:8::", "1::2::3", ":1::", "1:", "1::2:", "12345::",
		"g::1", "fe80::1%3", "[::1", "[::1]8088", "[::1]:", "[1.2.3.4]", "::1.2.3", "1:2:3:4:5:6:7:1.2.3.4",
		"[::1]:99999", "::1.2.3.4:5" })
	{
		CHECK_SILENT(!Engine::parseNetAddress(bad, a));
	}
	NetAddress b;
	CHECK(Engine::parseNetAddress("2001:db8::7", a) && Engine::parseNetAddress("2001:0db8:0::0:7", b) && a == b);
	b.port = 1;
	CHECK(a != b);

	// host names
	CHECK(Engine::isValidHostName("localhost") && Engine::isValidHostName("gzh-neo.example.org") && Engine::isValidHostName("a.b."));
	CHECK(Engine::isValidHostName("3com.com") && Engine::isValidHostName("x1"));
	for (const char *bad : { "", ".", "-a", "a-", "a..b", "a_b", "a b", "1.2.3", "1.2.3.4", "999.1.1.1", "ex!ample" })
	{
		CHECK_SILENT(!Engine::isValidHostName(bad));
	}
	CHECK(Engine::isValidHostName(std::string(63, 'a')) && !Engine::isValidHostName(std::string(64, 'a')));
	std::string longName;
	for (int i = 0; i < 64; ++i)
		longName += "abc.";
	CHECK(!Engine::isValidHostName(longName));

	// what a player types
	NetTarget t = Engine::parseNetTarget(" game.example.org ");
	CHECK(t.kind == NetTarget::Kind::HostName && t.hostName == "game.example.org" && t.port == 0);
	t = Engine::parseNetTarget("game.example.org:8088");
	CHECK(t.kind == NetTarget::Kind::HostName && t.hostName == "game.example.org" && t.port == 8088);
	t = Engine::parseNetTarget("192.168.0.5");
	CHECK(t.kind == NetTarget::Kind::Address && t.address.ipv4() == 0xC0A80005u);
	t = Engine::parseNetTarget("[2001:db8::1]:9");
	CHECK(t.kind == NetTarget::Kind::Address && t.address.isV6() && t.port == 9);
	for (const char *bad : { "", "1.2.3", "300.1.1.1", "host:", "host:0", "host:1:2", "[host]", "bad_name", "a b" })
	{
		CHECK_SILENT(Engine::parseNetTarget(bad).kind == NetTarget::Kind::Invalid);
	}

	// no reads past the text: parse every prefix of a long input from a buffer without terminator
	const char text[] = "[2001:db8:1:2:3:4:5:6]:12345";
	int accepted = 0;
	for (std::size_t n = 0; n <= sizeof(text) - 1; ++n)
	{
		std::vector<char> copy(text, text + n);
		accepted += Engine::parseNetAddress(std::string_view(copy.data(), copy.size()), a) ? 1 : 0;
	}
	CHECK(accepted == 6);	// "[...]" and the port prefixes ":1" to ":12345"
}


// ------------------------------------------------------------------ NetAddressBook

static void testNetAddressBook()
{
	std::printf("NetAddressBook\n");
	using Engine::NetAddress;
	using Engine::NetAddressBook;

	auto addr = [](const char *text) {
		NetAddress a;
		Engine::parseNetAddress(text, a);
		return a;
	};

	// handles: IPv4 is itself, IPv6 lands in the reserved range, the same on every machine
	const NetAddress v6a = addr("2001:db8::1");
	const NetAddress v6b = addr("2001:db8::2");
	CHECK(NetAddressBook::handleOf(addr("192.168.0.7")) == 0xC0A80007u);
	const std::uint32_t ha = NetAddressBook::handleOf(v6a);
	const std::uint32_t hb = NetAddressBook::handleOf(v6b);
	CHECK(NetAddressBook::isHandle(ha) && NetAddressBook::isHandle(hb) && ha != hb);
	CHECK(!NetAddressBook::isHandle(0xFFFFFFFFu) && !NetAddressBook::isHandle(0xC0A80007u));
	CHECK(NetAddressBook::handleOf(addr("::ffff:10.0.0.1")) == 0x0A000001u);
	CHECK(NetAddressBook::handleOf(NetAddress()) == 0);
	{
		int outside = 0;
		for (unsigned i = 0; i < 20000; ++i)
		{
			NetAddress x = v6a;
			x.bytes[12] = static_cast<std::uint8_t>(i >> 8);
			x.bytes[15] = static_cast<std::uint8_t>(i);
			outside += NetAddressBook::isHandle(NetAddressBook::handleOf(x)) ? 0 : 1;
		}
		CHECK(outside == 0);
	}

	// a LAN: identities are the addresses, routes go to the slot port
	NetAddressBook book;
	NetAddress out;
	CHECK(book.identityOf(addr("192.168.0.7:8088")) == 0xC0A80007u);
	CHECK(book.route(0xC0A80007u, 8088, 8088, out) && out.toString() == "192.168.0.7:8088");
	CHECK(book.route(0xFFFFFFFFu, 8086, 8086, out) && out.toString() == "255.255.255.255:8086");

	// an IPv6 peer: known after its first packet, unknown before
	CHECK(!book.route(hb, 8088, 8088, out));
	CHECK(book.identityOf(addr("[2001:db8::2]:8088")) == hb);
	CHECK(book.route(hb, 8088, 8088, out) && out.toString() == "[2001:db8::2]:8088");
	// the host names the others; a handle that does not match its address is refused
	CHECK(book.registerAddress(v6a) == ha);
	CHECK(book.addressOf(ha, out) && out == v6a);

	// NAT: replies go to the port the peer's packets come from, per local port
	book.observe(0xC6336401u, 8088, addr("198.51.100.1:61000"));
	CHECK(book.route(0xC6336401u, 8088, 8088, out) && out.toString() == "198.51.100.1:61000");
	CHECK(book.route(0xC6336401u, 8086, 8086, out) && out.toString() == "198.51.100.1:8086");
	book.observe(0xC6336401u, 8088, addr("198.51.100.1:61001"));
	CHECK(book.route(0xC6336401u, 8088, 8088, out) && out.port == 61001);

	// another local port has its own observation
	CHECK(book.route(0xC6336401u, 8086, 8086, out) && out.port == 8086);
	// our socket moves to another port: the observed route goes along, the old one stays
	book.moveLocalPort(8088, 9088);
	CHECK(book.route(0xC6336401u, 8088, 9088, out) && out.port == 61001);
	CHECK(book.route(0xC6336401u, 8088, 8088, out) && out.port == 61001);
	book.moveLocalPort(9088, 9088);
	CHECK(book.route(0xC6336401u, 8088, 9088, out) && out.port == 61001);
	CHECK(book.addressOf(ha, out) && out == v6a);

	// the tables stay bounded when anyone sends
	for (unsigned i = 0; i < 5000; ++i)
	{
		NetAddress x = v6b;
		x.bytes[13] = static_cast<std::uint8_t>(i >> 8);
		x.bytes[14] = static_cast<std::uint8_t>(i);
		book.identityOf(x);
		book.observe(i, 8088, x);
	}
	book.clear();
	CHECK(!book.addressOf(ha, out));
}

// ------------------------------------------------------------------ RateLimiter

static void testNetAddressBookBindings()
{
	std::printf("NetAddressBook bindings\n");
	Engine::NetAddressBook book;
	const Engine::NetAddress lan = Engine::NetAddress::fromIPv4(0xC0A80014u, 8088);
	const Engine::NetAddress nat = Engine::NetAddress::fromIPv4(0xCB007107u, 40001);
	CHECK(book.identityOf(nat, 8088) == 0xCB007107u);	// unknown: the address itself
	book.observe(0xF1234567u, 8088, nat);
	CHECK(book.identityOf(nat, 8088) == 0xF1234567u);	// the key identity behind this endpoint
	CHECK(book.identityOf(nat, 8086) == 0xCB007107u);	// other local port: not bound there
	Engine::NetAddress other = nat;
	other.port = 40002;
	CHECK(book.identityOf(other, 8088) == 0xCB007107u);	// other port of the same host
	Engine::NetAddress to;
	CHECK(book.route(0xF1234567u, 8088, 8088, to) && to == nat);
	book.observe(0xF1234567u, 8088, lan);	// moved: the newest endpoint counts
	CHECK(book.identityOf(lan, 8088) == 0xF1234567u && book.identityOf(nat, 8088) == 0xCB007107u);
}

static void testRateLimiter()
{
	std::printf("RateLimiter\n");
	using Engine::RateLimiter;
	using Engine::RateLimit;

	// not configured: everything passes
	{
		RateLimiter off;
		bool all = true;
		for (int i = 0; i < 10000; ++i)
			all = all && off.allow(1, 0);
		CHECK(all);
	}

	RateLimiter rl;
	rl.configure({ 100, 20 }, { 50, 10 });

	// burst, then the rate
	int passed = 0;
	for (int i = 0; i < 100; ++i)
		passed += rl.allow(7, 1000) ? 1 : 0;
	CHECK(passed == 20);
	CHECK(!rl.allow(7, 1009));			// 0.9 packets refilled
	CHECK(rl.allow(7, 1010));			// one after 10 ms at 100/s
	passed = 0;
	for (std::uint32_t t = 1010; t < 2010; ++t)	// one second: about 100 packets, not more
		passed += rl.allow(7, t + 1) ? 1 : 0;
	CHECK(passed >= 95 && passed <= 101);
	CHECK(rl.allow(8, 2010));			// another sender has its own budget

	// the clock wraps around
	RateLimiter wrap;
	wrap.configure({ 100, 1 }, { 0, 0 });
	CHECK(wrap.allow(3, 0xFFFFFFF0u) && !wrap.allow(3, 0xFFFFFFF5u) && wrap.allow(3, 0x00000001u));

	// forged senders: beyond the table they share one small budget, players keep theirs
	RateLimiter flood;
	flood.configure({ 100, 20 }, { 50, 10 });
	flood.setTrusted(0xC0A80002u, true, 0);
	std::uint64_t forgedPassed = 0;
	int playerPassed = 0;
	for (std::uint32_t t = 0; t < 1000; ++t)
	{
		for (std::uint32_t k = 0; k < 200; ++k)
			forgedPassed += flood.allow(0x0A000000u + t * 200 + k, t) ? 1 : 0;
		playerPassed += flood.allow(0xC0A80002u, t) ? 1 : 0;
	}
	// 63 own buckets get their burst once (20 each), the shared bucket 10 plus 50 per second
	CHECK(forgedPassed <= 63u * 20u + 10u + 50u + 5u);
	CHECK(playerPassed >= 95);
	CHECK(flood.stats().droppedShared > 190000u);

	// an idle untrusted bucket is reused after a while, a trusted one never
	RateLimiter reuse;
	reuse.configure({ 10, 1 }, { 0, 0 });
	reuse.setTrusted(1, true, 0);
	for (std::uint32_t k = 2; k < 2 + RateLimiter::MAX_TRACKED - 1; ++k)
		reuse.allow(k, 0);
	CHECK(!reuse.allow(999, 100));	// table full, no shared budget
	CHECK(reuse.allow(999, 20000));	// buckets idle for 20 s are reused
	CHECK(reuse.allow(1, 20000));	// the player still has its own
	reuse.clearTrusted();
}

// ------------------------------------------------------------------ PacketSeal fingerprints

static void testSealFingerprint()
{
	std::printf("PacketSeal fingerprints\n");
	std::uint8_t pk[32];
	fromHex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", pk, 32);
	char code[10];
	Engine::sealFingerprint(pk, code);
	CHECK(std::string(code) == "DP94-1QG2");	// BLAKE2b-40 in Crockford base 32, checked with Python
	CHECK(Engine::sealFingerprintMatches("DP94-1QG2", pk));
	CHECK(Engine::sealFingerprintMatches("dp941qg2", pk));
	CHECK(Engine::sealFingerprintMatches(" DP94 1QG2 ", pk));
	CHECK(Engine::sealFingerprintMatches("DP94-IQG2", pk) && Engine::sealFingerprintMatches("DP94-lqg2", pk));	// I, L read as 1
	for (const char *bad : std::initializer_list<const char *>{ "", "DP94", "DP94-1QG", "DP94-1QG22", "DP94-1QG3", "XP94-1QG2", nullptr })
	{
		CHECK_SILENT(!Engine::sealFingerprintMatches(bad, pk));
	}
	std::uint8_t other[32];
	std::memcpy(other, pk, 32);
	other[31] ^= 1;
	Engine::sealFingerprint(other, code);
	CHECK(std::string(code) != "DP94-1QG2" && !Engine::sealFingerprintMatches("DP94-1QG2", other));
	CHECK(Engine::isSealFingerprintText(code) && Engine::isSealFingerprintText("dp94 iqg2"));
	CHECK(!Engine::isSealFingerprintText("DP94-1QG") && !Engine::isSealFingerprintText("DP94-1QGU") && !Engine::isSealFingerprintText(nullptr));
}

// ------------------------------------------------------------------ PortMapping

static void testPortMapping()
{
	std::printf("PortMapping\n");
	namespace PM = Engine::PortMapping;

	// NAT-PMP (RFC 6886 layouts)
	std::uint8_t request[12];
	CHECK(PM::natpmpAddressRequest(request) == 2 && request[0] == 0 && request[1] == 0);
	CHECK(PM::natpmpMapUdpRequest(8088, 8088, 3600, request) == 12);
	const std::uint8_t expectedMap[12] = { 0, 1, 0, 0, 0x1F, 0x98, 0x1F, 0x98, 0, 0, 0x0E, 0x10 };
	CHECK(std::memcmp(request, expectedMap, 12) == 0);
	const std::uint8_t addressAnswer[12] = { 0, 128, 0, 0, 0, 0, 0x01, 0x00, 203, 0, 113, 7 };
	PM::NatPmpResponse r;
	CHECK(PM::natpmpParse(addressAnswer, 12, r) && r.op == 128 && r.result == 0 && r.epoch == 256 && r.externalIP == 0xCB007107u);
	CHECK(!PM::natpmpParse(addressAnswer, 11, r));	// cut short
	const std::uint8_t mapAnswer[16] = { 0, 129, 0, 0, 0, 0, 0, 5, 0x1F, 0x98, 0x1F, 0x99, 0, 0, 0x0E, 0x10 };
	CHECK(PM::natpmpParse(mapAnswer, 16, r) && r.op == 129 && r.internalPort == 8088 && r.externalPort == 8089 && r.lifetime == 3600);
	const std::uint8_t refused[8] = { 0, 129, 0, 2, 0, 0, 0, 5 };	// "not authorized", short form
	CHECK(PM::natpmpParse(refused, 8, r) && r.result == 2);
	const std::uint8_t notAnswer[12] = { 0, 1, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4 };	// a request, not a response
	const std::uint8_t otherVersion[12] = { 2, 128, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4 };	// PCP
	CHECK(!PM::natpmpParse(notAnswer, 12, r) && !PM::natpmpParse(otherVersion, 12, r) && !PM::natpmpParse(nullptr, 12, r));

	// SSDP
	const std::string search = PM::ssdpSearchRequest();
	CHECK(search.find("M-SEARCH * HTTP/1.1\r\n") == 0 && search.find("InternetGatewayDevice:1\r\n\r\n") != std::string::npos);
	const char answer[] = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=1800\r\nlocation:  http://192.168.178.1:49000/igddesc.xml \r\nST: x\r\n\r\n";
	std::string location;
	CHECK(PM::ssdpLocation(answer, sizeof(answer) - 1, location) && location == "http://192.168.178.1:49000/igddesc.xml");
	const char notify[] = "NOTIFY * HTTP/1.1\r\nLOCATION: http://10.0.0.1/\r\n\r\n";
	const char noLocation[] = "HTTP/1.1 200 OK\r\nST: x\r\n\r\n";
	CHECK(!PM::ssdpLocation(notify, sizeof(notify) - 1, location) && !PM::ssdpLocation(noLocation, sizeof(noLocation) - 1, location));

	// URLs
	PM::HttpUrl url;
	CHECK(PM::parseHttpUrl("http://192.168.178.1:49000/igdupnp/control/WANIPConn1", url) && url.host == "192.168.178.1"
		&& url.port == 49000 && url.path == "/igdupnp/control/WANIPConn1");
	CHECK(PM::parseHttpUrl("HTTP://router.local", url) && url.host == "router.local" && url.port == 80 && url.path == "/");
	for (const char *bad : std::initializer_list<const char *>{ "https://1.2.3.4/", "http://", "http://a@b/", "http://[::1]/",
		"http://1.2.3.4:0/", "http://1.2.3.4:65536/", "http://1.2.3.4/a b", "http://1.2.3.4/a\r\nX: y", "ftp://1.2.3.4/" })
	{
		CHECK_SILENT(!PM::parseHttpUrl(bad, url));
	}
	CHECK(PM::resolveUrl("http://10.0.0.1:5000/desc/root.xml", "/ctl/IPConn") == "http://10.0.0.1:5000/ctl/IPConn");
	CHECK(PM::resolveUrl("http://10.0.0.1:5000/desc/root.xml", "ctl/IPConn") == "http://10.0.0.1:5000/desc/ctl/IPConn");
	CHECK(PM::resolveUrl("http://10.0.0.1:5000/desc/root.xml", " http://10.0.0.2:80/x ") == "http://10.0.0.2:80/x");
	CHECK(PM::resolveUrl("http://10.0.0.1:5000/", "file:///etc/passwd").empty() && PM::resolveUrl("nonsense", "/x").empty());

	// HTTP responses
	PM::HttpResponse response;
	CHECK(PM::parseHttpResponse("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello-and-more", response)
		&& response.status == 200 && response.body == "hello");
	CHECK(PM::parseHttpResponse("HTTP/1.1 500 Internal\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nabcd\r\n3;x=y\r\nefg\r\n0\r\n\r\n", response)
		&& response.status == 500 && response.body == "abcdefg");
	CHECK(PM::parseHttpResponse("HTTP/1.0 200 OK\r\nServer: x\r\n\r\nto the end", response) && response.body == "to the end");
	CHECK(!PM::parseHttpResponse("HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nshort", response));
	CHECK(!PM::parseHttpResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFF\r\nab", response));
	CHECK(!PM::parseHttpResponse("SIP/2.0 200 OK\r\n\r\n", response) && !PM::parseHttpResponse("HTTP/1.1 200 OK\r\nX: y", response));
	// complete by its length before the connection closes (routers that keep it open), unknown without one
	CHECK(PM::parseHttpResponse("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", response) && response.framed);
	CHECK(PM::parseHttpResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\n0\r\n\r\n", response) && response.framed);
	CHECK(!PM::parseHttpResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\n", response));
	CHECK(PM::parseHttpResponse("HTTP/1.0 200 OK\r\n\r\nmore may come", response) && !response.framed);
	// other headers of a search answer: the router's name for the log
	std::string server;
	const char named[] = "HTTP/1.1 200 OK\r\nLOCATION: http://192.168.0.1:5000/rootDesc.xml\r\nServer: Linux/4.9 UPnP/1.1 MiniUPnPd/2.1\r\n\r\n";
	CHECK(PM::ssdpHeader(named, sizeof(named) - 1, "SERVER:", server) && server == "Linux/4.9 UPnP/1.1 MiniUPnPd/2.1"
		&& !PM::ssdpHeader(named, sizeof(named) - 1, "USN:", server));
	CHECK(PM::ssdpSearchRequest().find("MX: 1\r\n") != std::string::npos);	// answers come within the caller's wait
	// where to ask the gateway directly: the FRITZ!Box first, every entry a port and an absolute path
	CHECK(PM::knownDescription(0).port == 49000 && std::string(PM::knownDescription(0).path) == "/igddesc.xml"
		&& PM::knownDescription(99).port == 49000);
	bool knownWellFormed = true;
	for (int i = 0; i < PM::KNOWN_DESCRIPTION_COUNT; ++i)
		knownWellFormed = knownWellFormed && PM::knownDescription(i).port != 0 && PM::knownDescription(i).path[0] == '/';
	CHECK(knownWellFormed);

	// device description: the IP connection wins over the PPP one, relative control URL
	const std::string description =
		"<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device>"
		"<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType><serviceList>"
		"<service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
		"<controlURL>/ctl/L3F</controlURL></service></serviceList><deviceList><device><deviceList><device><serviceList>"
		"<service>\n<serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>"
		"<controlURL>/ctl/PPPConn</controlURL></service>"
		"<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
		"<SCPDURL>/scpd.xml</SCPDURL><controlURL>ctl/IPConn</controlURL></service>"
		"</serviceList></device></deviceList></device></deviceList></device></root>";
	PM::IgdControl control;
	CHECK(PM::igdFindControl(description, "http://192.168.1.1:5000/rootDesc.xml", control)
		&& control.serviceType == "urn:schemas-upnp-org:service:WANIPConnection:1"
		&& control.controlUrl == "http://192.168.1.1:5000/ctl/IPConn");
	const std::string withBase = "<root><URLBase>http://192.168.1.1:49000/</URLBase><service><serviceType>"
		"urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType><controlURL>/upnp/control/wanpppconn1</controlURL></service></root>";
	CHECK(PM::igdFindControl(withBase, "http://192.168.1.1:1900/desc.xml", control)
		&& control.controlUrl == "http://192.168.1.1:49000/upnp/control/wanpppconn1");
	CHECK(!PM::igdFindControl("<root><service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
		"<controlURL>/x</controlURL></service></root>", "http://1.2.3.4/", control));
	CHECK(!PM::igdFindControl("<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>", "http://1.2.3.4/", control));
	// every connection service in order: a gateway may list one it does not use
	const std::vector<PM::IgdControl> all = PM::igdFindControls(description, "http://192.168.1.1:5000/rootDesc.xml");
	CHECK(all.size() == 2 && all[0].serviceType == "urn:schemas-upnp-org:service:WANIPConnection:1"
		&& all[1].controlUrl == "http://192.168.1.1:5000/ctl/PPPConn");
	CHECK(PM::igdFindControls("<root/>", "http://1.2.3.4/").empty());
	// searches for both device versions and both services
	CHECK(PM::ssdpSearchRequest(PM::ssdpSearchTarget(1)).find("ST: urn:schemas-upnp-org:device:InternetGatewayDevice:2\r\n") != std::string::npos
		&& std::string(PM::ssdpSearchTarget(3)) == "urn:schemas-upnp-org:service:WANPPPConnection:1"
		&& std::string(PM::ssdpSearchTarget(99)) == PM::ssdpSearchTarget(0));
	// IPv6 firewall of an IGD 2: pinholes from anywhere to one address, UDP, lease limited to a day
	const std::string igd2 = "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:2"
		"</serviceType><controlURL>/ctl/IPConn</controlURL></service><service><serviceType>"
		"urn:schemas-upnp-org:service:WANIPv6FirewallControl:1</serviceType><controlURL>/ctl/IP6FC</controlURL></service>"
		"</serviceList></device></root>";
	PM::IgdControl firewall;
	CHECK(PM::igdFindFirewallControl(igd2, "http://192.168.0.1:49000/igd2desc.xml", firewall)
		&& firewall.controlUrl == "http://192.168.0.1:49000/ctl/IP6FC" && !PM::igdFindFirewallControl(description, "http://1.2.3.4/", firewall));
	const std::string hole = PM::soapAddPinhole(firewall.serviceType, "2001:db8::5", 8088, 3600);
	CHECK(hole.find("<u:AddPinhole xmlns:u=\"urn:schemas-upnp-org:service:WANIPv6FirewallControl:1\">") != std::string::npos
		&& hole.find("<RemoteHost></RemoteHost><RemotePort>0</RemotePort><InternalClient>2001:db8::5</InternalClient>"
			"<InternalPort>8088</InternalPort><Protocol>17</Protocol><LeaseTime>3600</LeaseTime>") != std::string::npos);
	CHECK(PM::soapAddPinhole(firewall.serviceType, "2001:db8::5", 8088, 0).find("<LeaseTime>86400</LeaseTime>") != std::string::npos
		&& PM::soapUpdatePinhole(firewall.serviceType, 7, 999999).find("<UniqueID>7</UniqueID><NewLeaseTime>86400</NewLeaseTime>") != std::string::npos
		&& PM::soapDeletePinhole(firewall.serviceType, 7).find("<UniqueID>7</UniqueID>") != std::string::npos);
	PM::HttpResponse pinholeAnswer;
	pinholeAnswer.status = 200;
	pinholeAnswer.body = "<s:Envelope><s:Body><u:AddPinholeResponse><UniqueID> 12 </UniqueID></u:AddPinholeResponse></s:Body></s:Envelope>";
	PM::SoapResult pinholeSoap;
	CHECK(PM::parseSoapResponse(pinholeAnswer, pinholeSoap) && pinholeSoap.ok && pinholeSoap.uniqueId == 12 && pinholeSoap.firewallEnabled == -1);
	pinholeAnswer.body = "<FirewallEnabled>True</FirewallEnabled><InboundPinholeAllowed>0</InboundPinholeAllowed><UniqueID>70000</UniqueID>";
	CHECK(PM::parseSoapResponse(pinholeAnswer, pinholeSoap) && pinholeSoap.firewallEnabled == 1 && pinholeSoap.inboundPinholeAllowed == 0 && pinholeSoap.uniqueId == -1);
	pinholeAnswer.body = "<FirewallEnabled>maybe</FirewallEnabled>";
	CHECK(PM::parseSoapResponse(pinholeAnswer, pinholeSoap) && pinholeSoap.firewallEnabled == -1);
	// the external address of a router behind DS-Lite or carrier-grade NAT is no address to forward to
	CHECK(PM::isPublicIPv4(0xCB007107u) && PM::isPublicIPv4(0x08080808u));			// 203.0.113.7, 8.8.8.8
	CHECK(!PM::isPublicIPv4(0xC0000002u) && !PM::isPublicIPv4(0x64400001u) && !PM::isPublicIPv4(0xC0A80001u)
		&& !PM::isPublicIPv4(0) && !PM::isPublicIPv4(0x7F000001u) && !PM::isPublicIPv4(0xEFFFFFFAu));	// DS-Lite, CGNAT, LAN, none, loopback, multicast

	// SOAP
	const std::string add = PM::soapAddPortMapping(control.serviceType, 8088, 8088, "192.168.1.20", "GZH Neo <&>", 3600);
	CHECK(add.find("<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANPPPConnection:1\">") != std::string::npos
		&& add.find("<NewExternalPort>8088</NewExternalPort><NewProtocol>UDP</NewProtocol><NewInternalPort>8088</NewInternalPort>"
			"<NewInternalClient>192.168.1.20</NewInternalClient>") != std::string::npos
		&& add.find("GZH Neo &lt;&amp;&gt;") != std::string::npos && add.find("<NewLeaseDuration>3600</NewLeaseDuration>") != std::string::npos);
	PM::parseHttpUrl(control.controlUrl, url);
	const std::string post = PM::httpSoapRequest(url, control.serviceType, "AddPortMapping", add);
	CHECK(post.find("POST /upnp/control/wanpppconn1 HTTP/1.1\r\nHost: 192.168.1.1:49000\r\n") == 0
		&& post.find("SOAPAction: \"urn:schemas-upnp-org:service:WANPPPConnection:1#AddPortMapping\"\r\n") != std::string::npos
		&& post.find("Content-Length: " + std::to_string(add.size()) + "\r\n") != std::string::npos
		&& post.size() > add.size() && post.compare(post.size() - add.size(), add.size(), add) == 0);
	PM::SoapResult soap;
	response.status = 200;
	response.body = "<s:Envelope><s:Body><u:GetExternalIPAddressResponse><NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>"
		"</u:GetExternalIPAddressResponse></s:Body></s:Envelope>";
	CHECK(PM::parseSoapResponse(response, soap) && soap.ok && soap.errorCode == 0 && soap.externalIP == 0xCB007107u);
	response.status = 500;
	response.body = "<s:Fault><detail><UPnPError><errorCode>725</errorCode><errorDescription>OnlyPermanentLeasesSupported"
		"</errorDescription></UPnPError></detail></s:Fault>";
	CHECK(PM::parseSoapResponse(response, soap) && !soap.ok && soap.errorCode == PM::UPNP_ONLY_PERMANENT_LEASES);
	response.status = 200;
	response.body = "<NewExternalIPAddress>203.0.113.999</NewExternalIPAddress>";
	CHECK(PM::parseSoapResponse(response, soap) && soap.externalIP == 0);

	// addresses
	CHECK(PM::ipv4Text(0xC0A8B214u) == "192.168.178.20");
	CHECK(PM::isPrivateIPv4(0x0A000001u) && PM::isPrivateIPv4(0xAC1F0001u) && PM::isPrivateIPv4(0xC0A80001u)
		&& PM::isPrivateIPv4(0x64400001u) && PM::isPrivateIPv4(0xA9FE0001u));
	CHECK(!PM::isPrivateIPv4(0xAC200001u) && !PM::isPrivateIPv4(0xCB007107u) && !PM::isPrivateIPv4(0x64800001u) && !PM::isPrivateIPv4(0x08080808u));
}

// ------------------------------------------------------------------ RunAhead

static void testRunAhead()
{
	std::printf("RunAhead\n");
	using Engine::LatencyEstimator;
	using Engine::RunAheadController;
	using Engine::RunAheadLimits;

	LatencyEstimator e;
	CHECK(!e.hasSamples() && e.high() == 0.0f);
	for (int i = 0; i < 200; ++i)
		e.addSample(0.100f);
	CHECK(e.hasSamples() && std::fabs(e.smoothed() - 0.100f) < 0.001f && e.deviation() < 0.001f);
	e.addSample(std::nanf(""));
	e.addSample(-1.0f);
	e.addSample(60.0f);
	CHECK(std::fabs(e.smoothed() - 0.100f) < 0.001f);	// nonsense ignored
	LatencyEstimator jitter;	// 100 ms with spikes to 300 ms every fifth sample
	for (int i = 0; i < 400; ++i)
		jitter.addSample(i % 5 == 0 ? 0.300f : 0.100f);
	CHECK(jitter.high() > 0.23f && jitter.smoothed() < 0.16f);	// asks for more than the average (0.14 s) in every phase

	RunAheadLimits limits;
	limits.minFrames = 6;
	limits.maxFrames = 64;
	limits.slackPercent = 10;
	limits.safetyFrames = 2;
	CHECK(RunAheadController::target(0.002f, 30, limits) == 6);		// LAN: the minimum
	CHECK(RunAheadController::target(0.200f, 30, limits) == 6);		// 3 frames * 1.1 -> 4, + 2
	CHECK(RunAheadController::target(0.400f, 30, limits) == 9);		// 6 * 1.1 = 6.6 -> 7, + 2
	CHECK(RunAheadController::target(1.000f, 30, limits) == 19);		// 15 * 1.1 = 16.5 -> 17, + 2
	CHECK(RunAheadController::target(100.0f, 30, limits) == 64 && RunAheadController::target(std::nanf(""), 30, limits) == 6);
	CHECK(RunAheadController::target(0.400f, 0, limits) == 6);		// no frame rate: treated as 1

	RunAheadController c;
	CHECK(c.next(10, 20) == 20);	// up at once
	int r = 20;
	for (int round = 1; round < RunAheadController::CALM_ROUNDS; ++round)
		r = c.next(r, 6);
	CHECK(r == 20);				// holds for a few rounds
	r = c.next(r, 6);
	CHECK(r == 13);				// then half the distance
	r = c.next(r, 6);
	CHECK(r == 10);
	CHECK(c.next(r, 12) == 12);	// a spike: up again at once, and the calm count starts over
	CHECK(c.next(12, 6) == 12);
	// from the start value 30 down to the LAN minimum within a few seconds of rounds
	RunAheadController lan;
	r = 30;
	int rounds = 0;
	while (r > 6 && rounds < 50)
	{
		r = lan.next(r, 6);
		++rounds;
	}
	CHECK(r == 6 && rounds <= RunAheadController::CALM_ROUNDS + 6);
}

// ------------------------------------------------------------------ Wire, SessionMessage

static void testWire()
{
	std::printf("Wire\n");
	using namespace Engine;
	std::uint8_t buf[64];
	WireWriter w(buf, sizeof(buf));
	w.u8(0xAB);
	w.u16(0x1234);
	w.u32(0xDEADBEEFu);
	w.u64(0x0102030405060708ull);
	w.text("abc", 8);
	w.endpoint(WireEndpoint::ipv4(0xC0A80001u, 8086));
	CHECK(w.ok() && w.size() == 1 + 2 + 4 + 8 + 4 + 7);
	WireReader r(buf, w.size());
	CHECK(r.u8() == 0xAB && r.u16() == 0x1234 && r.u32() == 0xDEADBEEFu && r.u64() == 0x0102030405060708ull);
	CHECK(r.text(8) == "abc");
	const WireEndpoint e = r.endpoint();
	CHECK(r.done() && e.ipv4Address() == 0xC0A80001u && e.port == 8086 && e.text() == "192.168.0.1:8086");

	WireWriter small(buf, 3);
	small.u32(1);
	CHECK(!small.ok());
	WireWriter longName(buf, sizeof(buf));
	longName.text("abcdefghi", 8);
	CHECK(!longName.ok());
	const std::uint8_t tooLong[] = { 5, 'a', 'b' };
	WireReader cut(tooLong, sizeof(tooLong));
	CHECK(cut.text(8).empty() && !cut.ok() && cut.u8() == 0);	// sticky
	const std::uint8_t control[] = { 2, 'a', 0x07 };
	WireReader ctl(control, sizeof(control));
	ctl.text(8);
	CHECK(!ctl.ok());
	const std::uint8_t badFamily[] = { 5, 1, 2, 3, 4, 0, 0 };
	WireReader fam(badFamily, sizeof(badFamily));
	fam.endpoint();
	CHECK(!fam.ok());
	const std::uint8_t badFlag[] = { 2 };
	WireReader fl(badFlag, 1);
	fl.flag();
	CHECK(!fl.ok());
}

static void testLocalNetworks()
{
	std::printf("LocalNetworks\n");
	using namespace Engine;
	CHECK(directedBroadcast({ 0xC0A8B214u, 24 }) == 0xC0A8B2FFu);		// 192.168.178.20/24
	CHECK(directedBroadcast({ 0x0A000005u, 8 }) == 0x0AFFFFFFu);
	CHECK(directedBroadcast({ 0x0A000005u, 31 }) == 0 && directedBroadcast({ 0x0A000005u, 0 }) == 0);
	const std::vector<LocalNetwork> usable = usableLanNetworks({ { 0x7F000001u, 8 }, { 0x5DB8D822u, 24 }, { 0xA9FE0101u, 16 },
		{ 0xC0A8B214u, 24 }, { 0xC0A8B214u, 24 }, { 0x0A000005u, 8 }, { 0, 0 } });
	CHECK(usable.size() == 3 && usable[0].address == 0xC0A8B214u && usable[1].address == 0x0A000005u
		&& usable[2].address == 0x5DB8D822u);	// private first, public after, loopback and link-local gone
	// a virtual adapter listed first (Hyper-V 172.20/16) gives way to the one with the default route
	const std::vector<LocalNetwork> routed = usableLanNetworks({ { 0xAC140001u, 20 }, { 0x5DB8D822u, 24 }, { 0xC0A800DEu, 24 } },
		0xC0A800DEu);
	CHECK(routed.size() == 3 && routed[0].address == 0xC0A800DEu && routed[1].address == 0xAC140001u
		&& routed[2].address == 0x5DB8D822u);
	CHECK(usableLanNetworks({ { 0x5DB8D822u, 24 }, { 0xC0A800DEu, 24 } }, 0x5DB8D822u)[0].address == 0xC0A800DEu);	// a public one stays behind
}

static void testRecentIds()
{
	std::printf("RecentIds\n");
	Engine::RecentIds<4> ids;
	CHECK(ids.firstTime(7) && ids.firstTime(8) && !ids.firstTime(7) && !ids.firstTime(8));
	CHECK(ids.firstTime(0) && !ids.firstTime(0));	// 0 is an ID like any other
	CHECK(ids.firstTime(65535) && ids.firstTime(9));	// 7 drops out (oldest of four)
	CHECK(ids.firstTime(7) && !ids.firstTime(9));
	ids.clear();
	CHECK(ids.firstTime(9));
}

static void testRemovalVote()
{
	std::printf("RemovalVote\n");
	using namespace Engine;
	using O = RemovalVote::Outcome;
	RemovalVote vote;
	CHECK(vote.vote(0, 1, 10, 0x03, 0x03) == O::Refused);		// two players: no vote
	CHECK(vote.vote(0, 0, 10, 0x07, 0x07) == O::Refused);		// not against oneself
	CHECK(vote.vote(0, 3, 10, 0x07, 0x07) == O::Refused && vote.vote(3, 1, 10, 0x07, 0x07) == O::Refused);	// not in the game
	CHECK(vote.vote(-1, 1, 10, 0xFF, 0xFF) == O::Refused && vote.vote(0, 8, 10, 0xFF, 0xFF) == O::Refused);
	CHECK(vote.vote(0, 2, 100, 0x0F, 0x0F) == O::Counted && vote.vote(0, 2, 101, 0x0F, 0x0F) == O::Counted);	// repeated: one vote
	CHECK(vote.tally(2, 101, 0x0F).votes == 1 && vote.tally(2, 101, 0x0F).needed == 3);
	CHECK(vote.vote(1, 2, 102, 0x0F, 0x0F) == O::Counted);
	CHECK(vote.vote(3, 2, 103, 0x0F, 0x0F) == O::Passed);		// every other player of four
	CHECK(vote.vote(3, 2, 103, 0x0B, 0x0B) == O::Refused);	// the target is gone
	vote.forget(2);
	CHECK(vote.tally(2, 104, 0x0F).votes == 0);

	RemovalVote late;	// a vote is valid for VOTE_FRAMES
	CHECK(late.vote(0, 2, 1000, 0x07, 0x07) == O::Counted);
	CHECK(late.vote(1, 2, 1000 + RemovalVote::VOTE_FRAMES, 0x07, 0x07) == O::Counted);	// the first one ran out
	CHECK(late.vote(0, 2, 1001 + RemovalVote::VOTE_FRAMES, 0x07, 0x07) == O::Passed);
	CHECK(late.vote(0, 1, 0, 0x07, 0x07) == O::Counted && late.tally(1, 0, 0x07).votes == 1);	// frame 0 counts

	RemovalVote leaving;	// a voter who left counts no more, and is no more needed
	CHECK(leaving.vote(0, 3, 50, 0x0F, 0x0F) == O::Counted && leaving.vote(1, 3, 50, 0x0F, 0x0F) == O::Counted);
	CHECK(leaving.tally(3, 51, 0x0E).votes == 1 && leaving.tally(3, 51, 0x0E).needed == 2);	// 0 left
	CHECK(leaving.vote(2, 3, 52, 0x0E, 0x0E) == O::Passed);	// 0 left: 1 and 2 are all the others

	RemovalVote watched;	// slot 2 observes: it does not vote, so two players cannot remove each other
	CHECK(watched.vote(2, 1, 10, 0x07, 0x03) == O::Refused);	// the observer may not vote
	CHECK(watched.vote(0, 1, 10, 0x07, 0x03) == O::Refused);	// one other voter is too few
	CHECK(watched.vote(0, 2, 10, 0x07, 0x03) == O::Counted && watched.vote(1, 2, 11, 0x07, 0x03) == O::Passed);	// the players remove the observer
	RemovalVote watched4;	// three players and an observer: the observer is not needed
	CHECK(watched4.vote(0, 2, 10, 0x0F, 0x07) == O::Counted && watched4.vote(1, 2, 10, 0x0F, 0x07) == O::Passed);
}

static void testPeerLink()
{
	std::printf("PeerLink\n");
	using namespace Engine;
	const std::uint32_t t0 = 0xFFFFF000u;	// across the wrap of the clock
	PeerLink link(0xF1000001u, 77, t0);
	CHECK(link.takeProbe(t0) && !link.takeProbe(t0 + 499) && link.takeProbe(t0 + 500));	// fast at first
	CHECK(link.update(t0 + 5999, true, false) == PeerLink::Change::None);
	CHECK(link.update(t0 + 6000, false, false) == PeerLink::Change::None);	// no relay bound yet
	CHECK(link.update(t0 + 6000, true, false) == PeerLink::Change::UseRelay && link.relayed());
	CHECK(link.update(t0 + 7000, true, false) == PeerLink::Change::None);	// once
	CHECK(link.answered(78, false) == PeerLink::Change::None && !link.direct());	// wrong nonce
	CHECK(link.answered(77, false) == PeerLink::Change::UseDirect && link.direct() && !link.relayed());
	CHECK(link.takeProbe(t0 + 20000) && !link.takeProbe(t0 + 24999) && link.takeProbe(t0 + 25000));	// slow once answered

	PeerLink quiet(0xF1000002u, 5, 1000);
	CHECK(quiet.takeProbe(1000) && quiet.takeProbe(11000) && !quiet.takeProbe(11600) && quiet.takeProbe(16000));	// slow after the fast phase

	PeerLink forced(0xF1000003u, 9, 0);
	CHECK(forced.update(1, true, true) == PeerLink::Change::UseRelay);
	CHECK(forced.answered(9, true) == PeerLink::Change::None && forced.direct() && forced.relayed());	// stays relayed

	PeerLink none;
	CHECK(!none.takeProbe(0) && none.update(99999, true, true) == PeerLink::Change::None);
}

static void testRendezvousServerList()
{
	std::printf("RendezvousServerList\n");
	using namespace Engine;
	RendezvousServerList list;
	CHECK(list.add("meet.example.org") && list.add("[2001:db8::1]:8090") && list.add("192.0.2.7:9000"));
	CHECK(!list.add("meet.example.org") && !list.add("") && !list.add("not a host!") && !list.add(std::string(97, 'a')));
	list.addLines("# comment\n  other.example.org:8090  # trailing\r\n\nmeet.example.org\nbad host\n");
	CHECK(list.servers().size() == 4 && list.servers()[3] == "other.example.org:8090");
	const std::vector<std::string> added = list.addFound({ "a.example.org", "meet.example.org", "b.example.org" });
	CHECK(added.size() == 2 && added[0] == "a.example.org" && list.contains("b.example.org"));

	std::vector<std::string> many;
	for (int i = 0; i < 70; ++i)
		many.push_back("s" + std::to_string(i) + ".example.org");
	const std::string file = RendezvousServerList::foundFile("# old\nold.example.org\n", many);
	RendezvousServerList reread;
	reread.addLines(file);
	CHECK(reread.servers().size() == RendezvousServerList::MAX_FOUND && reread.servers().back() == "s69.example.org"
		&& !reread.contains("old.example.org"));	// the newest are kept

	CHECK(RendezvousServerList::withServer("# mine\nmeet.example.org", "203.0.113.9:9000") == "# mine\nmeet.example.org\n203.0.113.9:9000\n");
	CHECK(RendezvousServerList::withServer("meet.example.org # home\n", "meet.example.org") == "meet.example.org # home\n");	// listed already
	CHECK(RendezvousServerList::withoutServer("# mine\nmeet.example.org # home\nother.example.org\n", "meet.example.org") == "# mine\nother.example.org\n");
	CHECK(list.remove("meet.example.org") && !list.contains("meet.example.org") && !list.remove("meet.example.org"));

	RendezvousServerList full;
	for (int i = 0; i < 200; ++i)
		full.add("h" + std::to_string(i) + ".example.org");
	CHECK(full.servers().size() == RendezvousServerList::MAX_SERVERS);
}

static void testSessionMessages()
{
	std::printf("SessionMessage\n");
	using namespace Engine;
	std::uint8_t buf[MAX_SESSION_MESSAGE];

	// every type survives the round trip; every cut and every extra byte is refused
	std::vector<SessionMessage> samples;
	for (std::uint8_t t = 1; t < 64; ++t)
	{
		if (!isKnownSessionType(t))
			continue;
		SessionMessage m;
		m.type = static_cast<SessionType>(t);
		m.protocol = 4;
		m.sessionId = 0x1122334455667788ull;
		m.programCRC = 0xA1;
		m.dataCRC = 0xB2;
		m.fingerprint[0] = 9;
		m.gameName = "Game";
		m.hostName = "Host";
		m.mapName = "maps\\alpine assault\\alpine assault.map";
		m.players = 2;
		m.maxPlayers = 4;
		m.inProgress = true;
		m.name = "Alice";
		m.chatKind = 1;
		m.slot = 3;
		m.text = "hello \xC3\xA4";
		m.key[0] = 0x42;
		m.nonce[7] = 7;
		m.cookie[15] = 15;
		m.sealed.assign(40, 0x5A);
		m.reason = 4;
		m.versionText = "1.0 (Sep 30 2026)";
		m.identity = 0xF1234567u;
		m.value = 77;
		m.revision = 5;
		m.options = "M=maps\\x;S=HAlice,F1234567,8088,FT,0,-1,-1,-1,1:;";
		m.member.slot = 2;
		m.member.identity = 0xF7654321u;
		m.member.key[31] = 1;
		m.member.endpoints.lobby[0] = WireEndpoint::ipv4(0x0A000002, 8086);
		m.member.endpoints.game[0] = WireEndpoint::ipv4(0xCB007107, 8088);
		m.member.endpoints.game[1] = WireEndpoint::ipv4(0x0A000002, 8088);
		m.flag = true;
		m.mapCRC = 0x1234;
		m.relayServer = "meet.example.org:8090";
		m.relayId = 99;
		m.relayToken[0] = 1;
		m.offset = 1792;
		m.markBytes = 2000;
		m.markFrames = 300;
		m.chunk.assign(SESSION_MAX_WATCH_CHUNK, 0x3C);
		m.token[0] = 0xA5;
		m.token[15] = 0x5A;
		m.returnState.frame = 4321;
		m.returnState.connected = 0x0B;
		m.returnState.crcPending = 0x08;
		m.returnState.router = 1;
		m.returnState.fallback[0] = 1;
		m.returnState.fallback[1] = 3;
		m.returnState.dropFrame[2] = 1200;
		m.returnState.runAhead = 12;
		m.returnState.frameRate = 30;
		samples.push_back(m);
	}
	CHECK(samples.size() == 30);
	int roundTrips = 0;
	int refusedCuts = 0;
	int cuts = 0;
	for (const SessionMessage &m : samples)
	{
		const std::size_t size = encodeSession(m, buf, sizeof(buf));
		SessionMessage back;
		if (size == 0 || !decodeSession(buf, size, back))
			continue;
		std::uint8_t again[MAX_SESSION_MESSAGE];
		const std::size_t size2 = encodeSession(back, again, sizeof(again));
		if (size2 == size && std::memcmp(buf, again, size) == 0 && back.type == m.type)
			++roundTrips;
		for (std::size_t cut = 0; cut < size; ++cut)
		{
			++cuts;
			SessionMessage x;
			refusedCuts += decodeSession(buf, cut, x) ? 0 : 1;
		}
		std::uint8_t longer[MAX_SESSION_MESSAGE + 1];
		std::memcpy(longer, buf, size);
		longer[size] = 0;
		SessionMessage x;
		CHECK_SILENT(!decodeSession(longer, size + 1, x));	// length field does not match
		longer[6] = static_cast<std::uint8_t>(longer[6] + 1);	// the length field claims the extra byte
		CHECK_SILENT(!decodeSession(longer, size + 1, x) || m.type == SessionType::BYE || m.type == SessionType::START
			|| m.type == SessionType::LEAVE || m.type == SessionType::HELLO || m.type == SessionType::JOIN);
	}
	CHECK(roundTrips == 30);
	CHECK(refusedCuts == cuts);

	SessionMessage challenge = samples[static_cast<std::size_t>(SessionType::CHALLENGE) - 1];
	SessionMessage hello = samples[static_cast<std::size_t>(SessionType::HELLO) - 1];
	CHECK(encodeSession(hello, buf, sizeof(buf)) >= encodeSession(challenge, buf, sizeof(buf)));	// no amplification

	SessionMessage bad = samples[0];
	bad.name = std::string(SESSION_MAX_NAME + 1, 'x');
	bad.type = SessionType::PRESENCE;
	CHECK(encodeSession(bad, buf, sizeof(buf)) == 0);
	bad.type = SessionType::STATE;
	bad.options = std::string(SESSION_MAX_OPTIONS + 1, 'o');
	CHECK(encodeSession(bad, buf, sizeof(buf)) == 0);
	bad.type = static_cast<SessionType>(20);
	CHECK(encodeSession(bad, buf, sizeof(buf)) == 0);

	SessionMessage ping;
	ping.type = SessionType::PING;
	std::size_t size = encodeSession(ping, buf, sizeof(buf));
	SessionMessage x;
	buf[5] = 1;	// reserved byte
	CHECK(size != 0 && !decodeSession(buf, size, x));
	buf[5] = 0;
	buf[3] = 2;	// other version
	CHECK(!decodeSession(buf, size, x));
	buf[3] = 1;
	buf[4] = 20;	// unknown type
	CHECK(!decodeSession(buf, size, x));
	CHECK(isSealedSessionType(SessionType::CHAT) && !isSealedSessionType(SessionType::JOIN));

	SessionJoinInfo info;
	info.name = "Bob";
	info.protocol = 4;
	info.programCRC = 1;
	info.dataCRC = 2;
	info.versionText = "1.0";
	info.endpoints.game[0] = WireEndpoint::ipv4(0x0A000003, 8088);
	size = encodeJoinInfo(info, buf, sizeof(buf));
	SessionJoinInfo infoBack;
	CHECK(size != 0 && decodeJoinInfo(buf, size, infoBack) && infoBack.name == "Bob" && infoBack.endpoints.game[0].port == 8088);
	CHECK(!decodeJoinInfo(buf, size - 1, infoBack));
	CHECK(infoBack.wants == SessionWants::PLAY);
	info.wants = SessionWants::PLAY_OR_WATCH;
	size = encodeJoinInfo(info, buf, sizeof(buf));
	CHECK(size != 0 && decodeJoinInfo(buf, size, infoBack) && infoBack.wants == SessionWants::PLAY_OR_WATCH);
	buf[size - 1] = 4;	// no such wish
	CHECK(!decodeJoinInfo(buf, size, infoBack));
	buf[size - 1] = static_cast<std::uint8_t>(SessionWants::RETURN);	// the slot and the token are missing
	CHECK(!decodeJoinInfo(buf, size, infoBack));
	info.wants = SessionWants::RETURN;
	info.returnSlot = 3;
	info.returnToken[0] = 0x11;
	info.returnToken[15] = 0xEE;
	size = encodeJoinInfo(info, buf, sizeof(buf));
	CHECK(size != 0 && decodeJoinInfo(buf, size, infoBack) && infoBack.wants == SessionWants::RETURN && infoBack.returnSlot == 3
		&& infoBack.returnToken[0] == 0x11 && infoBack.returnToken[15] == 0xEE);
	CHECK(!decodeJoinInfo(buf, size - 1, infoBack));

	// the state at a return frame and the welcome back
	SessionMessage state;
	state.type = SessionType::RETURN_STATE;
	state.returnState.frame = 900;
	state.returnState.connected = 0x05;
	state.returnState.router = 2;
	state.returnState.fallback[0] = 2;
	state.returnState.fallback[1] = 0;
	state.returnState.dropFrame[1] = 640;
	state.returnState.runAhead = 15;
	state.returnState.frameRate = 30;
	size = encodeSession(state, buf, sizeof(buf));
	CHECK(size != 0 && decodeSession(buf, size, x) && x.returnState.frame == 900 && x.returnState.connected == 0x05
		&& x.returnState.router == 2 && x.returnState.fallback[1] == 0 && x.returnState.fallback[2] == 0xFF
		&& x.returnState.dropFrame[1] == 640 && x.returnState.runAhead == 15);
	SessionMessage welcome;
	welcome.type = SessionType::WELCOME;
	welcome.slot = 2;
	welcome.flag = true;
	welcome.token[3] = 0x33;
	size = encodeSession(welcome, buf, sizeof(buf));
	CHECK(size != 0 && decodeSession(buf, size, x) && x.flag && x.slot == 2 && x.token[3] == 0x33);
	CHECK(isSealedSessionType(SessionType::RETURN_READY) && isSealedSessionType(SessionType::RETURN_DONE));

	// a replay chunk: at most SESSION_MAX_WATCH_CHUNK, the size field must match
	SessionMessage watch;
	watch.type = SessionType::WATCH;
	watch.chunk.assign(SESSION_MAX_WATCH_CHUNK + 1, 1);
	CHECK(encodeSession(watch, buf, sizeof(buf)) == 0);
	watch.chunk.assign(10, 1);
	size = encodeSession(watch, buf, sizeof(buf));
	CHECK(size != 0 && decodeSession(buf, size, x) && x.chunk.size() == 10);
	buf[SESSION_HEADER_SIZE + 13] = 11;	// claims one byte more than there is
	CHECK(!decodeSession(buf, size, x));
	CHECK(isSealedSessionType(SessionType::WATCH) && isSealedSessionType(SessionType::WATCH_ACK));
	// a list claiming four endpoints
	std::uint8_t four[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4 };
	CHECK(!decodeJoinInfo(four, sizeof(four), infoBack));
}

static void testSessionHandshake()
{
	std::printf("SessionHandshake\n");
	using namespace Engine;
	namespace HS = Engine::SessionHandshake;
	SealKeyPair host;
	SealKeyPair joiner;
	SealKeyPair stranger;
	CHECK(host.generate() && joiner.generate() && stranger.generate());
	std::uint8_t cookieKey[32];
	std::memset(cookieKey, 0x11, sizeof(cookieKey));
	std::uint8_t nonce[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	const WireEndpoint from = WireEndpoint::ipv4(0x0A000002, 8086);
	std::uint8_t c1[16], c2[16], c3[16], c4[16];
	HS::cookie(cookieKey, 5, joiner.publicKey, nonce, from, c1);
	HS::cookie(cookieKey, 5, joiner.publicKey, nonce, from, c2);
	HS::cookie(cookieKey, 5, joiner.publicKey, nonce, WireEndpoint::ipv4(0x0A000003, 8086), c3);
	cookieKey[0] ^= 1;
	HS::cookie(cookieKey, 5, joiner.publicKey, nonce, from, c4);
	CHECK(HS::cookieEquals(c1, c2) && !HS::cookieEquals(c1, c3) && !HS::cookieEquals(c1, c4));

	const std::uint8_t plain[] = "name=Bob";
	std::vector<std::uint8_t> sealed;
	std::vector<std::uint8_t> opened;
	CHECK(HS::sealJoin(joiner, host.publicKey, nonce, plain, sizeof(plain), sealed));
	CHECK(sealed.size() == sizeof(plain) + HS::JOIN_SEAL_OVERHEAD);
	CHECK(HS::openJoin(host, joiner.publicKey, nonce, sealed.data(), sealed.size(), opened)
		&& opened.size() == sizeof(plain) && std::memcmp(opened.data(), plain, sizeof(plain)) == 0);
	CHECK(!HS::openJoin(stranger, joiner.publicKey, nonce, sealed.data(), sealed.size(), opened));	// other host
	CHECK(!HS::openJoin(host, stranger.publicKey, nonce, sealed.data(), sealed.size(), opened));	// other joiner key
	nonce[0] ^= 1;
	CHECK(!HS::openJoin(host, joiner.publicKey, nonce, sealed.data(), sealed.size(), opened));
	nonce[0] ^= 1;
	int flips = 0;
	for (std::size_t i = 0; i < sealed.size(); ++i)
	{
		sealed[i] ^= 0x20;
		flips += HS::openJoin(host, joiner.publicKey, nonce, sealed.data(), sealed.size(), opened) ? 0 : 1;
		sealed[i] ^= 0x20;
	}
	CHECK(flips == static_cast<int>(sealed.size()));
	const std::uint8_t zeroKey[32] = {};
	CHECK(!HS::sealJoin(joiner, zeroKey, nonce, plain, sizeof(plain), sealed));	// low-order point

	const std::uint32_t id = HS::identityOf(joiner.publicKey);
	CHECK(id >= 0xF0000000u && id <= 0xFEFFFFFFu && id == HS::identityOf(joiner.publicKey) && id != HS::identityOf(host.publicKey));
}

static void testInviteCode()
{
	std::printf("InviteCode\n");
	using namespace Engine;
	InviteCode c;
	CHECK(parseInviteCode("K7Q2-DP941QG2@meet.example.org", c) && c.room == "K7Q2" && c.fingerprint == "DP941QG2"
		&& c.server.kind == NetTarget::Kind::HostName && c.server.hostName == "meet.example.org" && c.serverPort() == 8090);
	CHECK(parseInviteCode(" k7q2-dp94-iqg2@203.0.113.7:9000 ", c) && c.fingerprint == "DP941QG2"
		&& c.server.kind == NetTarget::Kind::Address && c.serverPort() == 9000);
	CHECK(parseInviteCode("K7Q2DP941QG2", c) && c.server.kind == NetTarget::Kind::Invalid && c.room == "K7Q2");
	CHECK(parseInviteCode("KOQL-DP941QG2", c) && c.room == "K0Q1");
	for (const char *bad : { "", "K7Q2", "K7Q2-DP941QG", "K7Q2-DP941QG2@", "K7QU-DP941QG2", "K7Q2-DP94-1QG2-X",
		"K7Q2-DP941QG2@bad host", "192.168.0.1", "K7Q2-DP941QG2@-x.org" })
	{
		CHECK_SILENT(!parseInviteCode(bad, c));
	}
	CHECK(formatInviteCode("K7Q2", "DP941QG2", "meet.example.org", 8090) == "K7Q2-DP941QG2@meet.example.org");
	CHECK(formatInviteCode("K7Q2", "DP941QG2", "2001:db8::1", 9000) == "K7Q2-DP941QG2@[2001:db8::1]:9000");
	CHECK(formatInviteCode("K7Q2", "DP941QG2", "", 0) == "K7Q2-DP941QG2");
	CHECK(inviteRoomFromBits(0) == "0000" && inviteRoomFromBits(0xFFFFF) == "ZZZZ");
	CHECK(parseInviteCode(formatInviteCode(inviteRoomFromBits(0x12345), "DP941QG2", "a.b", 1234), c) && c.serverPort() == 1234);
}

// ------------------------------------------------------------------ Rendezvous

static void testRendezvousMessages()
{
	std::printf("Rendezvous messages\n");
	using namespace Engine;
	std::uint8_t buf[MAX_TREFF_MESSAGE];
	int roundTrips = 0;
	int types = 0;
	for (std::uint8_t t = static_cast<std::uint8_t>(TreffType::OBSERVE); t <= static_cast<std::uint8_t>(TreffType::REFUSED); ++t)
	{
		++types;
		TreffMessage m;
		m.type = static_cast<TreffType>(t);
		m.tag = 0x77;
		m.protocol = 4;
		m.room = "K7Q2";
		m.owner[3] = 3;
		m.socket = TreffSocket::GAME;
		m.isPublic = true;
		m.game.room = "K7Q2";
		m.game.gameName = "G";
		m.game.fingerprint = "DP941QG2";
		m.game.players = 2;
		m.observed = WireEndpoint::ipv4(0xCB007107, 40000);
		m.lobby[0] = WireEndpoint::ipv4(0xC0A80014, 8086);
		m.gameSocket[0] = WireEndpoint::ipv4(0xC0A80014, 8088);
		m.games.push_back(m.game);
		m.servers.push_back("meet.example.org");
		m.relayId = 5;
		TreffRelayMember member;
		member.identity = 0xF0000001u;
		m.relayMembers.push_back(member);
		m.identity = 0xF0000001u;
		m.reason = TreffRefusal::LIMIT;
		m.padding = 10;
		const std::size_t size = encodeTreff(m, buf, sizeof(buf));
		TreffMessage back;
		std::uint8_t again[MAX_TREFF_MESSAGE];
		if (size != 0 && decodeTreff(buf, size, back) && encodeTreff(back, again, sizeof(again)) == size
			&& std::memcmp(buf, again, size) == 0)
		{
			++roundTrips;
		}
		for (std::size_t cut = 0; cut < size; ++cut)
		{
			TreffMessage x;
			CHECK_SILENT(!decodeTreff(buf, cut, x));
		}
	}
	CHECK(roundTrips == types && types == 17);
	TreffMessage lookup;
	lookup.type = TreffType::LOOKUP;
	lookup.room = "K7Q2";
	padTreffRequest(lookup, TREFF_LOOKUP_SIZE);
	CHECK(encodeTreff(lookup, buf, sizeof(buf)) == TREFF_LOOKUP_SIZE);
	TreffMessage list;
	list.type = TreffType::LIST;
	padTreffRequest(list, TREFF_LIST_SIZE);
	CHECK(encodeTreff(list, buf, sizeof(buf)) == TREFF_LIST_SIZE);

	RelayHeader h;
	h.relayId = 0xABCDEF;
	h.from = 1;
	h.to = 2;
	std::uint8_t frame[RELAY_HEADER_SIZE];
	writeRelayHeader(h, frame);
	RelayHeader back;
	CHECK(looksLikeRelayFrame(frame, sizeof(frame)) && readRelayHeader(frame, sizeof(frame), back)
		&& back.relayId == h.relayId && back.from == 1 && back.to == 2);
	CHECK(!readRelayHeader(frame, sizeof(frame) - 1, back));
}

namespace
{
	std::vector<std::uint8_t> treff(Engine::TreffMessage m, std::size_t padTo = 0)
	{
		if (padTo != 0)
			Engine::padTreffRequest(m, padTo);
		std::vector<std::uint8_t> out(Engine::MAX_TREFF_MESSAGE);
		out.resize(Engine::encodeTreff(m, out.data(), out.size()));
		return out;
	}

	bool answer(const std::vector<Engine::RendezvousServer::Datagram> &out, std::size_t index, Engine::TreffMessage &m)
	{
		return index < out.size() && Engine::decodeTreff(out[index].data.data(), out[index].data.size(), m);
	}
}

static void testRendezvousServer()
{
	std::printf("RendezvousServer\n");
	using namespace Engine;
	RendezvousServer server;
	RendezvousServer::Config config;
	config.roomsPerAddress = 2;
	config.relayBytesPerSecond = 3000;
	config.peers = { "meet.example.org", "203.0.113.9:9000" };
	server.configure(config);
	std::vector<RendezvousServer::Datagram> out;
	const WireEndpoint host = WireEndpoint::ipv4(0xCB007107, 40000);
	const WireEndpoint hostGame = WireEndpoint::ipv4(0xCB007107, 40001);
	const WireEndpoint joiner = WireEndpoint::ipv4(0xC6336401, 50000);
	const WireEndpoint joinerGame = WireEndpoint::ipv4(0xC6336401, 50001);
	const WireEndpoint stranger = WireEndpoint::ipv4(0x08080808, 1234);
	std::uint32_t now = 1000;
	TreffMessage a;

	// observe: answered only when padded to the answer's size
	TreffMessage observe;
	observe.type = TreffType::OBSERVE;
	observe.tag = 9;
	std::vector<std::uint8_t> d = treff(observe);
	server.handle(joiner, d.data(), d.size(), now, out);
	CHECK(out.empty());
	d = treff(observe, TREFF_OBSERVE_SIZE);
	server.handle(joiner, d.data(), d.size(), now, out);
	CHECK(answer(out, 0, a) && a.type == TreffType::OBSERVED && a.tag == 9 && a.observed == joiner && out[0].data.size() <= d.size());
	out.clear();

	// register from both sockets
	TreffMessage reg;
	reg.type = TreffType::REGISTER;
	reg.room = "K7Q2";
	reg.owner[0] = 0xAA;
	reg.isPublic = true;
	reg.game.gameName = "Open";
	reg.lobby[0] = WireEndpoint::ipv4(0xC0A80014, 8086);
	d = treff(reg);
	server.handle(host, d.data(), d.size(), now, out);
	CHECK(answer(out, 0, a) && a.type == TreffType::REGISTER_ACK && a.observed == host && server.roomCount() == 1);
	reg.socket = TreffSocket::GAME;
	d = treff(reg);
	server.handle(hostGame, d.data(), d.size(), now, out);
	CHECK(answer(out, 1, a) && a.type == TreffType::REGISTER_ACK && a.socket == TreffSocket::GAME);
	out.clear();

	// somebody else cannot take the room or add a game socket to it
	TreffMessage steal = reg;
	steal.owner[0] = 0xBB;
	steal.socket = TreffSocket::LOBBY;
	d = treff(steal);
	server.handle(stranger, d.data(), d.size(), now, out);
	CHECK(answer(out, 0, a) && a.type == TreffType::REFUSED && a.reason == TreffRefusal::ROOM_TAKEN);
	steal.socket = TreffSocket::GAME;
	d = treff(steal);
	server.handle(stranger, d.data(), d.size(), now, out);
	CHECK(answer(out, 1, a) && a.type == TreffType::REFUSED && a.reason == TreffRefusal::NO_ROOM);
	out.clear();

	// lookup: the joiner gets the host's endpoints, the host a knock with the joiner's
	TreffMessage lookup;
	lookup.type = TreffType::LOOKUP;
	lookup.tag = 3;
	lookup.room = "K7Q2";
	lookup.gameSocket[0] = joinerGame;
	d = treff(lookup);
	server.handle(joiner, d.data(), d.size(), now, out);
	CHECK(out.empty());	// not padded: no answer
	d = treff(lookup, TREFF_LOOKUP_SIZE);
	server.handle(joiner, d.data(), d.size(), now, out);
	TreffMessage knock;
	CHECK(out.size() == 2 && answer(out, 0, a) && a.type == TreffType::FOUND && a.lobby[0] == host && a.gameSocket[0] == hostGame
		&& a.lobby[1].port == 8086 && out[0].data.size() <= d.size());
	CHECK(answer(out, 1, knock) && knock.type == TreffType::KNOCK && out[1].to == host && knock.lobby[0] == joiner
		&& knock.gameSocket[0] == joinerGame);
	out.clear();
	for (int i = 0; i < 10; ++i)
		server.handle(joiner, d.data(), d.size(), now, out);
	int knocks = 0;
	for (const auto &x : out)
		knocks += x.to == host ? 1 : 0;
	CHECK(knocks == static_cast<int>(config.knocksPerRoomPerSecond) - 1);	// one went out with the first lookup
	out.clear();
	lookup.room = "ZZZZ";
	d = treff(lookup, TREFF_LOOKUP_SIZE);
	server.handle(joiner, d.data(), d.size(), now, out);
	CHECK(answer(out, 0, a) && a.type == TreffType::REFUSED && a.reason == TreffRefusal::NO_ROOM);
	out.clear();

	// rooms per address
	reg.socket = TreffSocket::LOBBY;
	reg.room = "AAAA";
	d = treff(reg);
	server.handle(host, d.data(), d.size(), now, out);
	reg.room = "BBBB";
	d = treff(reg);
	server.handle(host, d.data(), d.size(), now, out);
	CHECK(answer(out, 1, a) && a.type == TreffType::REFUSED && a.reason == TreffRefusal::LIMIT && server.roomCount() == 2);
	out.clear();
	reg.room = "bad!";
	d = treff(reg);
	server.handle(host, d.data(), d.size(), now, out);
	CHECK(out.empty());

	// public list and servers, only when padded
	TreffMessage list;
	list.type = TreffType::LIST;
	d = treff(list, TREFF_LIST_SIZE);
	server.handle(stranger, d.data(), d.size(), now, out);
	CHECK(answer(out, 0, a) && a.type == TreffType::GAMES && a.games.size() == 2 && out[0].data.size() <= d.size());
	list.type = TreffType::SERVERS;
	d = treff(list, TREFF_LIST_SIZE);
	server.handle(stranger, d.data(), d.size(), now, out);
	CHECK(answer(out, 1, a) && a.type == TreffType::SERVER_LIST && a.servers.size() == 2 && a.servers[1] == "203.0.113.9:9000");
	out.clear();

	// relay: opened by the host, bound with tokens, forwards only between bound members
	const std::uint32_t idHost = 0xF0000001u;
	const std::uint32_t idJoiner = 0xF0000002u;
	std::uint8_t tokenHost[16] = { 1 };
	std::uint8_t tokenJoiner[16] = { 2 };
	TreffMessage open;
	open.type = TreffType::RELAY_OPEN;
	open.room = "K7Q2";
	open.owner[0] = 0xAA;
	open.relayId = 77;
	TreffRelayMember mh;
	mh.identity = idHost;
	relayTokenHash(tokenHost, mh.tokenHash);
	TreffRelayMember mj;
	mj.identity = idJoiner;
	relayTokenHash(tokenJoiner, mj.tokenHash);
	open.relayMembers = { mh, mj };
	d = treff(open);
	server.handle(stranger, d.data(), d.size(), now, out);
	CHECK(answer(out, 0, a) && a.type == TreffType::REFUSED);	// only from the host's endpoint
	server.handle(host, d.data(), d.size(), now, out);
	CHECK(answer(out, 1, a) && a.type == TreffType::RELAY_OPENED && server.relayCount() == 1);
	out.clear();
	TreffMessage bind;
	bind.type = TreffType::RELAY_BIND;
	bind.relayId = 77;
	bind.identity = idJoiner;
	std::memcpy(bind.token, tokenHost, 16);	// wrong token
	d = treff(bind);
	server.handle(joiner, d.data(), d.size(), now, out);
	CHECK(answer(out, 0, a) && a.type == TreffType::REFUSED && a.reason == TreffRefusal::BAD_TOKEN);
	std::memcpy(bind.token, tokenJoiner, 16);
	bind.socket = TreffSocket::GAME;
	d = treff(bind);
	server.handle(joinerGame, d.data(), d.size(), now, out);
	CHECK(answer(out, 1, a) && a.type == TreffType::RELAY_BOUND && a.observed == joinerGame);
	bind.identity = idHost;
	std::memcpy(bind.token, tokenHost, 16);
	d = treff(bind);
	server.handle(hostGame, d.data(), d.size(), now, out);
	out.clear();

	std::vector<std::uint8_t> frame(RELAY_HEADER_SIZE + 100, 0x33);
	RelayHeader h;
	h.relayId = 77;
	h.from = idHost;
	h.to = idJoiner;
	writeRelayHeader(h, frame.data());
	server.handle(hostGame, frame.data(), frame.size(), now, out);
	CHECK(out.size() == 1 && out[0].to == joinerGame && out[0].data == frame);
	out.clear();
	server.handle(stranger, frame.data(), frame.size(), now, out);	// not the binding of "from"
	server.handle(host, frame.data(), frame.size(), now, out);	// the host's lobby socket is not bound
	h.to = 0xF0000003u;	// no such member
	writeRelayHeader(h, frame.data());
	server.handle(hostGame, frame.data(), frame.size(), now, out);
	CHECK(out.empty() && server.stats().relayDropped == 3);
	h.to = idJoiner;
	writeRelayHeader(h, frame.data());
	for (int i = 0; i < 40; ++i)
		server.handle(hostGame, frame.data(), frame.size(), now, out);
	CHECK(out.size() == 3000 / frame.size() - 1);	// budget per second, one frame went through before
	out.clear();
	server.handle(hostGame, frame.data(), frame.size(), now + 1000, out);
	CHECK(out.size() == 1);	// next second
	out.clear();

	// expiry
	server.expire(now + config.roomTtlMs + 1001);
	CHECK(server.roomCount() == 0 && server.relayCount() == 1);
	server.expire(now + 1000 + config.relayTtlMs + 1);
	CHECK(server.relayCount() == 0);

	// garbage and a sender over its budget
	const std::uint8_t junk[] = "GZT1 no message";
	server.handle(stranger, junk, sizeof(junk), now, out);
	CHECK(out.empty() && server.stats().malformed >= 1);
	d = treff(observe, TREFF_OBSERVE_SIZE);
	for (int i = 0; i < 300; ++i)
		server.handle(stranger, d.data(), d.size(), now + 5000000, out);
	CHECK(out.size() <= static_cast<std::size_t>(config.perSender.burst));

	// relay switched off
	RendezvousServer noRelay;
	RendezvousServer::Config off;
	off.relay = false;
	noRelay.configure(off);
	out.clear();
	reg.room = "K7Q2";
	d = treff(reg);
	noRelay.handle(host, d.data(), d.size(), now, out);
	d = treff(open);
	noRelay.handle(host, d.data(), d.size(), now, out);
	CHECK(answer(out, 1, a) && a.type == TreffType::REFUSED && a.reason == TreffRefusal::RELAY_OFF);
}

// The replay stream to spectators: every byte arrives once and in order over a lossy line, a
// lost chunk is sent again, marks only allow frames whose bytes are there, the delay holds back.
static void testWatchStream()
{
	using namespace Engine;
	std::vector<std::uint8_t> log(20000);
	for (std::size_t i = 0; i < log.size(); ++i)
		log[i] = static_cast<std::uint8_t>(i * 31 + 7);

	// lossless: all of it in chunks of at most WATCH_CHUNK
	{
		WatchSender sender;
		WatchReceiver receiver;
		std::vector<std::uint8_t> got;
		std::uint32_t now = 0;
		std::uint32_t offset = 0, length = 0;
		int rounds = 0;
		while (receiver.received() < log.size() && rounds++ < 1000)
		{
			while (sender.nextChunk(static_cast<std::uint32_t>(log.size()), now, offset, length))
			{
				check(length > 0 && length <= WATCH_CHUNK, "watch: chunk size");
				receiver.accept(offset, log.data() + offset, length, got);
			}
			sender.acknowledge(receiver.received(), now);
			now += 50;
		}
		check(got == log, "watch: lossless stream complete and in order");
		check(sender.acknowledged() == log.size(), "watch: sender sees everything acknowledged");
		check(!sender.nextChunk(static_cast<std::uint32_t>(log.size()), now + 10000, offset, length), "watch: nothing to send when all is acknowledged");
	}

	// every third chunk lost, acknowledgements lost too: resent until complete
	{
		WatchSender sender;
		WatchReceiver receiver;
		std::vector<std::uint8_t> got;
		std::uint32_t now = 0;
		std::uint32_t offset = 0, length = 0;
		int n = 0;
		int rounds = 0;
		while (receiver.received() < log.size() && rounds++ < 5000)
		{
			while (sender.nextChunk(static_cast<std::uint32_t>(log.size()), now, offset, length))
			{
				if (++n % 3 != 0)
					receiver.accept(offset, log.data() + offset, length, got);
			}
			if (n % 5 != 0)
				sender.acknowledge(receiver.received(), now);
			now += 100;
		}
		check(got == log, "watch: lossy stream complete and in order");
	}

	// window: no more than the window unacknowledged
	{
		WatchSender::Config config;
		config.windowBytes = 4000;
		WatchSender sender(config);
		std::uint32_t offset = 0, length = 0, total = 0;
		while (sender.nextChunk(static_cast<std::uint32_t>(log.size()), 0, offset, length))
			total += length;
		check(total == 4000, "watch: window limits unacknowledged bytes");
		sender.acknowledge(1000, 10);
		check(sender.nextChunk(static_cast<std::uint32_t>(log.size()), 10, offset, length) && offset == 4000 && length == 896, "watch: acknowledgement opens the window");
		sender.acknowledge(999999, 20);
		check(sender.acknowledged() == 1000, "watch: acknowledgement beyond what was sent is ignored");
		check(sender.nextChunk(static_cast<std::uint32_t>(log.size()), 20 + config.resendMs, offset, length) && offset == 1000 && length == 896, "watch: resend of the first missing chunk");
		check(!sender.nextChunk(static_cast<std::uint32_t>(log.size()), 20 + config.resendMs, offset, length) || offset != 1000, "watch: one resend per timeout");
		check(sender.nextChunk(static_cast<std::uint32_t>(log.size()), 20 + 2 * config.resendMs, offset, length) && offset == 1000, "watch: second head resend");
		check(sender.nextChunk(static_cast<std::uint32_t>(log.size()), 20 + 3 * config.resendMs, offset, length) && offset == 1000, "watch: then the whole window from the gap");
		check(sender.nextChunk(static_cast<std::uint32_t>(log.size()), 20 + 3 * config.resendMs, offset, length) && offset == 1896, "watch: window goes on after the gap");
	}

	// receiver: duplicates, overlaps and gaps
	{
		WatchReceiver receiver;
		std::vector<std::uint8_t> got;
		check(receiver.accept(100, log.data() + 100, 50, got) == 0 && receiver.received() == 0, "watch: chunk after a gap waits");
		check(receiver.accept(0, log.data(), 100, got) == 150, "watch: first chunk connects the early one");
		check(got.size() == 150 && std::equal(got.begin(), got.end(), log.begin()), "watch: early chunk in its place");
		got.clear();
		receiver.reset();
		check(receiver.accept(0, log.data(), 100, got) == 100, "watch: first chunk");
		check(receiver.accept(50, log.data() + 50, 100, got) == 50 && receiver.received() == 150, "watch: overlap adds only the new part");
		check(receiver.accept(0, log.data(), 100, got) == 0, "watch: duplicate adds nothing");
		check(receiver.accept(150, log.data(), WATCH_CHUNK + 1, got) == 0, "watch: oversized chunk refused");
		check(receiver.accept(150, nullptr, 10, got) == 0, "watch: no data refused");
		check(receiver.accept(150 + WatchReceiver::MAX_EARLY_BYTES, log.data(), 10, got) == 0, "watch: far early chunk dropped");
		check(receiver.accept(150, log.data() + 150, 0, got) == 0, "watch: empty chunk refused");
		check(got.size() == 150 && std::equal(got.begin(), got.end(), log.begin()), "watch: received bytes match");
	}

	// marks: only frames whose bytes are complete
	{
		WatchReceiver receiver;
		std::vector<std::uint8_t> got;
		check(receiver.playableFrames() == 0, "watch: nothing playable at first");
		check(receiver.noteMark({ 500, 10 }), "watch: mark taken");
		check(receiver.playableFrames() == 0, "watch: mark without its bytes is not playable");
		receiver.accept(0, log.data(), 400, got);
		check(receiver.playableFrames() == 0, "watch: still missing bytes of the mark");
		receiver.accept(400, log.data() + 400, 200, got);
		check(receiver.playableFrames() == 10, "watch: mark playable with its bytes");
		check(!receiver.noteMark({ 400, 20 }), "watch: mark with fewer bytes refused");
		check(!receiver.noteMark({ 700, 5 }), "watch: mark with fewer frames refused");
		check(receiver.noteMark({ 500, 10 }), "watch: the same mark again is fine");
		check(!receiver.noteMark({ WATCH_MAX_BYTES + 1, 30 }), "watch: mark beyond the limit refused");
		check(receiver.noteMark({ 600, 40 }) && receiver.playableFrames() == 40, "watch: mark of bytes already here is playable at once");
		for (std::uint32_t i = 1; i <= 200; ++i)
			receiver.noteMark({ 600 + i * 10, 40 + i });
		check(receiver.playableFrames() == 40, "watch: many pending marks");
		receiver.accept(600, log.data() + 600, 896, got);
		receiver.accept(1496, log.data() + 1496, 896, got);
		check(receiver.playableFrames() < 240, "watch: merged mark waits for its bytes");
		receiver.accept(2392, log.data() + 2392, 896, got);
		check(receiver.playableFrames() == 240, "watch: merged marks become playable with the newest");
	}

	// the host's history and the delay
	{
		WatchHistory history;
		check(history.delayed(0).bytes == 0 && history.delayed(0).frames == 0, "watch: empty history");
		history.record(100, 1);
		history.record(100, 30);	// no commands: frames go on
		history.record(250, 31);
		history.record(250, 90);
		history.record(400, 100);
		history.record(300, 120);	// backwards: ignored
		const WatchMark now = history.delayed(0);
		check(now.bytes == 400 && now.frames == 100, "watch: newest mark without delay");
		const WatchMark a = history.delayed(10);
		check(a.bytes == 250 && a.frames == 90, "watch: delay 10 frames");
		const WatchMark b = history.delayed(70);
		check(b.bytes == 100 && b.frames == 30, "watch: delay 70 frames");
		const WatchMark c = history.delayed(99);
		check(c.bytes == 0 && c.frames == 0, "watch: delay before the first mark");
		check(history.delayed(1000).frames == 0, "watch: delay longer than the game");
	}
}

int main(int argc, char **argv)
{
	// --vfs-last <accesses>: only the file system load test, larger (a step of its own in the gate)
	if (argc == 3 && std::strcmp(argv[1], "--vfs-last") == 0)
	{
		std::printf("EngineTests: Vfs under load\n");
		runVfsLoad(std::atoi(argv[2]), 8);
		std::printf("\n%d checks, %d failures\n", s_checks, s_failures);
		return (s_failures == 0) ? 0 : 1;
	}
	std::printf("EngineTests\n\n");

	testClock();
	testThreadRole();
	testLogLevels();
	testLogRingBuffer();
	testLogRingBufferSnapshot();
	testLogThreadSafety();
	testLogWorkerAsserts();
	testCpuTopology();
	testStringRefCountProtocol();
	testDrawableLookupTable();
	testGenerationTable();
	testJobSystemBasics();
	testParallelDeterminism();
	testSimRandom();
	testVerifyParallel();
	testAsyncTask();
	testCombatModel();
	testAttackPlan();
	testFightControl();
	testLogicAccessGuard();
	testSimThread();
	testWatchdog();
	testTextureTier();
	testTexturePackage();
	testUserDataFiles();
	testBigArchive();
	testPreferenceFile();
	testWheelSteps();
	testScrollTiming();
	testReplayCrcQueue();
	testKeyLayout();
	testInputQueue();
	testSettingsSchema();
	testSettingsValues();
	testSettingsDocument();
	testSettingsStore();
	testSettingsImport();
	testSettingsTextConversions();
	testVfsPath();
	testVfsSources();
	testVfsMountPlan();
	testVfsIndex();
	testVfsLegacyEquivalence();
	testZipArchive();
	testVfsZipSource();
	testVfsModRanks();
	testPositionalFile();
	testVfsPrefetchPlan();
	testVfsLoad();
	testModCatalog();
	testTextFiles();
	testKeyScanCode();
	testWWFormat();
	testEngineFormatInto();
	testWideFormatCheck();
	testScopedGlobal();
	testEngineContext();
	testWndFont();
	testWndChunk();
	testWndQuotedValue();
	testWndNumbers();
	testWndBoolList();
	testWndLimits();
	testNetEmulator();
	testPacketSeal();
	testSealFingerprint();
	testNetAddress();
	testNetAddressBook();
	testNetAddressBookBindings();
	testRateLimiter();
	testPortMapping();
	testRunAhead();
	testWatchStream();
	testWire();
	testSessionMessages();
	testPeerLink();
	testRecentIds();
	testLocalNetworks();
	testRemovalVote();
	testRendezvousServerList();
	testSessionHandshake();
	testInviteCode();
	testRendezvousMessages();
	testRendezvousServer();

	std::printf("\n%d checks, %d failures\n", s_checks, s_failures);
	return (s_failures == 0) ? 0 : 1;
}
