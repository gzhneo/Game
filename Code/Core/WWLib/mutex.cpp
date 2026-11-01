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

#include "mutex.h"
#include "wwdebug.h"
#include <windows.h>


// ----------------------------------------------------------------------------

MutexClass::MutexClass(const char* name) : handle(nullptr), locked(false)
{
	handle=CreateMutex(nullptr,false,name);
	WWASSERT(handle);
}

MutexClass::~MutexClass()
{
	WWASSERT(!locked); // Can't delete locked mutex!
	CloseHandle(handle);
}

bool MutexClass::Lock(int time)
{
	int res = WaitForSingleObject(handle,time==WAIT_INFINITE ? INFINITE : time);
	if (res!=WAIT_OBJECT_0) return false;
	locked++;
	return true;
}

void MutexClass::Unlock()
{
	WWASSERT(locked);
	locked--;
	int res=ReleaseMutex(handle);
	res;	// silence compiler warnings
	WWASSERT(res);
}

// ----------------------------------------------------------------------------

MutexClass::LockClass::LockClass(MutexClass& mutex_,int time) : mutex(mutex_)
{
	failed=!mutex.Lock(time);
}

MutexClass::LockClass::~LockClass()
{
	if (!failed) mutex.Unlock();
}







// ----------------------------------------------------------------------------

CriticalSectionClass::CriticalSectionClass() : handle(nullptr), locked(false)
{
	handle=W3DNEWARRAY char[sizeof(CRITICAL_SECTION)];
	InitializeCriticalSection(static_cast<CRITICAL_SECTION*>(handle));
}

CriticalSectionClass::~CriticalSectionClass()
{
	WWASSERT(!locked); // Can't delete locked mutex!
	DeleteCriticalSection(static_cast<CRITICAL_SECTION*>(handle));
	delete[] static_cast<char*>(handle);
}

void CriticalSectionClass::Lock()
{
	EnterCriticalSection(static_cast<CRITICAL_SECTION*>(handle));
	locked++;
}

void CriticalSectionClass::Unlock()
{
	WWASSERT(locked);
	locked--;
	LeaveCriticalSection(static_cast<CRITICAL_SECTION*>(handle));
}

// ----------------------------------------------------------------------------

CriticalSectionClass::LockClass::LockClass(CriticalSectionClass& critical_section) : CriticalSection(critical_section)
{
	CriticalSection.Lock();
}

CriticalSectionClass::LockClass::~LockClass()
{
	CriticalSection.Unlock();
}


