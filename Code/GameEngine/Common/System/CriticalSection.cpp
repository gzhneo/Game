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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////


#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/CriticalSection.h"

#include <new>	// placement new, see below
#include "Engine/EngineLog.h"

// Definitions.
FastCriticalSectionClass TheAsciiStringCriticalSection;

// Locks are created on first use (see Common/CriticalSection.h).
//
// No plain `new` here: the engine's global `operator new` goes through
// TheDynamicMemoryAllocator, which takes exactly these locks:
//
//   GetDmaCriticalSection()                                  <- static variable being initialized
//     new CriticalSection()
//       operator new -> DynamicMemoryAllocator::allocateBytes
//         ScopedCriticalSection(TheDmaCriticalSection)
//           GetDmaCriticalSection()                           <- same variable, recursive
//
// The same holds for the pool lock (allocateBytes takes its blocks from MemoryPool, and
// MemoryPool::allocateBlock takes TheMemoryPoolCriticalSection). Recursive initialization of a
// function-local static is undefined: MSVC waits on itself in _Init_thread_wait (the game hangs
// at startup), libstdc++ aborts with recursive_init_error.
//
// Hence: static storage with the object built in by placement new. That allocates nothing and
// never enters the allocator. The destructor is intentionally never called; the locks must
// outlive all static destructors.
#define DEFINE_ENGINE_CRITICAL_SECTION( funcName, name )						\
	CriticalSection *funcName( void )											\
	{																			\
		alignas(CriticalSection) static char storage[sizeof(CriticalSection)];	\
		static CriticalSection *cs = new (static_cast<void *>(storage)) CriticalSection(name);	\
		return cs;																\
	}

DEFINE_ENGINE_CRITICAL_SECTION( GetUnicodeStringCriticalSection, "UnicodeString" )
DEFINE_ENGINE_CRITICAL_SECTION( GetDmaCriticalSection, "DMA" )
DEFINE_ENGINE_CRITICAL_SECTION( GetMemoryPoolCriticalSection, "MemoryPool" )
DEFINE_ENGINE_CRITICAL_SECTION( GetDebugLogCriticalSection, "DebugLog" )

// Engine lock statistics at exit.
void Engine_LogCriticalSectionStats( void )
{
#ifdef ENGINE_LOCK_STATS
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	CriticalSection *locks[] = { GetMemoryPoolCriticalSection(), GetDmaCriticalSection(), GetUnicodeStringCriticalSection(), GetDebugLogCriticalSection() };
	for (CriticalSection *cs : locks)
	{
		const double waitMs = static_cast<double>(cs->getWaitTicks()) * 1000.0 / static_cast<double>(freq.QuadPart);
		ENGINE_LOG_INFO(::Engine::LOG_CAT_MEMORY, "lock %s: %llu acquisitions, %llu contended, total wait %.3f ms",
			cs->getName(), cs->getAcquisitions(), cs->getContended(), waitMs);
	}
#endif
}


