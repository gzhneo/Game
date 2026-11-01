/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "thread.h"
#include "wwdebug.h"
#include <process.h>
#include <windows.h>
#pragma warning ( push )
#pragma warning ( disable : 4201 )
#include "systimer.h"
#include "Lib/SafeString.h"
#pragma warning ( pop )


ThreadClass::ThreadClass(const char *thread_name, ExceptionHandlerType exception_handler) : handle(0), running(false), thread_priority(0)
{
	if (thread_name) {
		assert(strlen(thread_name) < sizeof(ThreadName) - 1);
		safe_strcpy(ThreadName, thread_name);
	} else {
		safe_strcpy(ThreadName, "No name");;
	}

	ExceptionHandler = exception_handler;
}

ThreadClass::~ThreadClass()
{
	Stop();
}

void __cdecl ThreadClass::Internal_Thread_Function(void* params)
{
	ThreadClass* tc=reinterpret_cast<ThreadClass*>(params);
	tc->running=true;
	tc->ThreadID = GetCurrentThreadId();

#ifdef _WIN32

	if (tc->ExceptionHandler != nullptr) {
		__try {
			tc->Thread_Function();
		} __except(tc->ExceptionHandler(GetExceptionCode(), GetExceptionInformation())) {};
	} else {
		tc->Thread_Function();
	}

#else //_WIN32
	tc->Thread_Function();
#endif //_WIN32

	tc->handle=0;
	tc->ThreadID = 0;
}

void ThreadClass::Execute()
{
	WWASSERT(!handle);	// Only one thread at a time!
	handle=_beginthread(&Internal_Thread_Function,0,this);
	SetThreadPriority(reinterpret_cast<HANDLE>(handle),THREAD_PRIORITY_NORMAL+thread_priority);
	WWDEBUG_SAY(("ThreadClass::Execute: Started thread %s, thread ID is %llX\n", ThreadName, static_cast<unsigned long long>(handle)));
}

void ThreadClass::Set_Priority(int priority)
{
	thread_priority=priority;
	if (handle) SetThreadPriority(reinterpret_cast<HANDLE>(handle),THREAD_PRIORITY_NORMAL+thread_priority);
}

void ThreadClass::Stop(unsigned ms)
{
	running=false;
	unsigned time=TIMEGETTIME();
	while (handle) {
		if ((TIMEGETTIME()-time)>ms) {
			int res=TerminateThread(reinterpret_cast<HANDLE>(handle),0);
			res;	// just to silence compiler warnings
			WWASSERT(res);	// Thread still not killed!
			handle=0;
		}
		Sleep(0);
	}
}

void ThreadClass::Sleep_Ms(unsigned ms)
{
	Sleep(ms);
}

HANDLE test_event = ::CreateEvent (nullptr, FALSE, FALSE, "");

void ThreadClass::Switch_Thread()
{
	//	::SwitchToThread ();
	::WaitForSingleObject (test_event, 1);
	//	Sleep(1);	// Note! Parameter can not be 0 (or the thread switch doesn't occur)
}

// Return calling thread's unique thread id
unsigned ThreadClass::_Get_Current_Thread_ID()
{
	return GetCurrentThreadId();
}

bool ThreadClass::Is_Running()
{
	return !!handle;
}