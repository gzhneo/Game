/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/
// SimThread.cpp -- see Engine/SimThread.h.

#include "Engine/SimThread.h"
#include "Engine/ThreadRole.h"

#if defined(_WIN32)
	#include <windows.h>
#endif

namespace Engine
{

namespace
{
	SimThread::RenderOwnerHook s_renderOwnerHook = nullptr;

	unsigned currentThreadId()
	{
#if defined(_WIN32)
		return static_cast<unsigned>(::GetCurrentThreadId());
#else
		return static_cast<unsigned>(std::hash<std::thread::id>()(std::this_thread::get_id()));
#endif
	}
}

void SimThread::setRenderOwnerHook(RenderOwnerHook hook)
{
	s_renderOwnerHook = hook;
}

SimThread::~SimThread()
{
	stop();
}

bool SimThread::start()
{
	if (m_thread.joinable())
		return false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_quit = false;
		m_call = nullptr;
		m_finished = false;
	}
	m_thread = std::thread([this]() { threadMain(); });
	m_threadId = m_thread.get_id();
	return true;
}

void SimThread::stop()
{
	if (!m_thread.joinable())
		return;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_quit = true;
	}
	m_wake.notify_one();
	m_thread.join();
	m_threadId = std::thread::id();
}

bool SimThread::isCurrentThread() const
{
	return m_thread.joinable() && std::this_thread::get_id() == m_threadId;
}

void SimThread::runAndWait(const std::function<void()> &call)
{
	std::exception_ptr error;
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_call = &call;
		m_callerThreadId = currentThreadId();
		m_finished = false;
		m_error = nullptr;
		m_wake.notify_one();
		m_done.wait(lock, [this]() { return m_finished; });
		m_call = nullptr;
		error = m_error;
		m_error = nullptr;
	}
	if (error)
		std::rethrow_exception(error);
}

void SimThread::threadMain()
{
	setThreadRole(THREAD_ROLE_SIM, "Sim");
	const unsigned self = currentThreadId();
	std::unique_lock<std::mutex> lock(m_mutex);
	for (;;)
	{
		m_wake.wait(lock, [this]() { return m_quit || (m_call != nullptr && !m_finished); });
		if (m_quit)
			return;
		const std::function<void()> *call = m_call;
		const unsigned caller = m_callerThreadId;
		lock.unlock();

		std::exception_ptr error;
		if (s_renderOwnerHook != nullptr)
			s_renderOwnerHook(self);
		try
		{
			(*call)();
		}
		catch (...)
		{
			error = std::current_exception();
		}
		if (s_renderOwnerHook != nullptr)
			s_renderOwnerHook(caller);

		lock.lock();
		m_error = error;
		++m_runCount;
		m_finished = true;
		m_done.notify_one();
	}
}

SimThread &simThread()
{
	static SimThread s_simThread;
	return s_simThread;
}

} // namespace Engine
