/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/
// SimThread.h -- the simulation thread.
//
// The logic tick runs on its own thread (role THREAD_ROLE_SIM). For now the main thread hands the
// tick over and waits until it is done: logic and client still run one after the other, only on two
// threads. That proves the logic does not depend on the main thread before the two overlap.
//
// While the main thread waits, the simulation thread may use what is otherwise reserved for the main
// thread (rendering device, texture loader): the render-owner hook hands that right over and back.
#pragma once
#ifndef ENGINE_SIMTHREAD_H
#define ENGINE_SIMTHREAD_H

#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace Engine
{

class SimThread
{
public:
	SimThread() = default;
	~SimThread();
	SimThread(const SimThread &) = delete;
	SimThread &operator=(const SimThread &) = delete;

	/// Starts the thread. Returns FALSE if it is already running.
	bool start();
	/// Ends the thread (waits for it). Safe to call when not running.
	void stop();
	bool isRunning() const { return m_thread.joinable(); }

	/// Runs the call on the simulation thread; the caller blocks until it has returned. An exception
	/// thrown by the call is thrown again in the caller. Must not be called from the simulation thread.
	void runAndWait(const std::function<void()> &call);

	/// TRUE on the simulation thread.
	bool isCurrentThread() const;
	/// Number of calls run so far.
	unsigned long long runCount() const { return m_runCount; }

	/// Called on the simulation thread before and after each call with the thread that may use the
	/// rendering device meanwhile (the simulation thread, then the caller again). nullptr = none.
	using RenderOwnerHook = void (*)(unsigned threadId);
	static void setRenderOwnerHook(RenderOwnerHook hook);

private:
	void threadMain();

	std::thread m_thread;
	std::thread::id m_threadId;
	std::mutex m_mutex;
	std::condition_variable m_wake;
	std::condition_variable m_done;
	const std::function<void()> *m_call = nullptr;
	unsigned m_callerThreadId = 0;
	bool m_finished = false;
	bool m_quit = false;
	std::exception_ptr m_error;
	unsigned long long m_runCount = 0;
};

/// The one simulation thread of the game.
SimThread &simThread();

} // namespace Engine

#endif // ENGINE_SIMTHREAD_H
