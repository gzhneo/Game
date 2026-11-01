/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Watchdog.h -- detects hangs instead of sitting them out.
//
// A hung thread looks like a crashed game to the player but leaves nothing behind: no dump, no
// log, no cause. A sim thread and job workers add deadlocks as a failure class.
//
// The watchdog runs on its own, very cheap thread. Main and sim thread report once per loop with
// `heartbeat()`. If a report is missing for longer than the timeout (default 10 s), the watchdog
// calls the registered callback **once** -- that is where the platform work happens: dump all
// threads, save the log ring buffer, ask the user.
//
// The watchdog itself is portable and has no engine dependencies so it can be unit tested
// (short timeout, artificial hang).

#pragma once

#ifndef ENGINE_WATCHDOG_H
#define ENGINE_WATCHDOG_H

#include <functional>

namespace Engine
{

/// What is monitored. The names appear in the report.
enum WatchdogSubject
{
	WATCHDOG_MAIN = 0,		///< main thread (window, input, rendering)
	WATCHDOG_SIM,			///< simulation thread (logic tick); may be the main thread
	WATCHDOG_SUBJECT_COUNT
};

/// Callback on a hang: the subject and how long it has been silent.
using WatchdogCallback = std::function<void(WatchdogSubject subject, unsigned stalledMilliseconds)>;

class Watchdog
{
public:
	static Watchdog &get();

	/// Starts monitoring. `timeoutMs` is the timeout per subject, `pollMs` the watchdog thread's
	/// poll interval. Starting again with other values reconfigures.
	void start(unsigned timeoutMs = 10000, unsigned pollMs = 250);
	void stop();
	bool isRunning() const;

	/// Sets the callback (write dump, save log, ask the user). Without a callback the hang is
	/// only logged.
	void setCallback(const WatchdogCallback &callback);

	/// Sign of life. Cheap: one atomic store.
	void heartbeat(WatchdogSubject subject);

	/// Temporarily stops monitoring a subject (loading screen, pause, debugger). Calls nest: the
	/// subject is monitored again after as many resume() as suspend() calls (loading a save game
	/// starts a new game inside).
	void suspend(WatchdogSubject subject);
	void resume(WatchdogSubject subject);

	/// Number of hangs reported so far (diagnostics and tests).
	unsigned stallCount() const;
	/// How long the subject has been silent (milliseconds).
	unsigned millisecondsSinceHeartbeat(WatchdogSubject subject) const;

	static const char *subjectName(WatchdogSubject subject);

	Watchdog();
	~Watchdog();

private:
	Watchdog(const Watchdog &);
	Watchdog &operator=(const Watchdog &);

	struct Impl;
	Impl *m_impl;
};

/// Suspends main and sim thread monitoring for its lifetime (map building, loading a save game).
class WatchdogSuspension
{
public:
	WatchdogSuspension()
	{
		Watchdog::get().suspend(WATCHDOG_SIM);
		Watchdog::get().suspend(WATCHDOG_MAIN);
	}
	~WatchdogSuspension()
	{
		Watchdog::get().resume(WATCHDOG_SIM);
		Watchdog::get().resume(WATCHDOG_MAIN);
	}
	WatchdogSuspension(const WatchdogSuspension &) = delete;
	WatchdogSuspension &operator=(const WatchdogSuspension &) = delete;
};

} // namespace Engine

#endif // ENGINE_WATCHDOG_H
