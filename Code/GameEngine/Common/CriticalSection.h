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

// CriticalSection.h ///////////////////////////////////////////////////////
// Utility class to use critical sections in areas of code.
// Author: JohnM And MattC, August 13, 2002

#pragma once

#ifndef __CRITICALSECTION_H__
#define __CRITICALSECTION_H__

#include "Common/PerfTimer.h"


// Lock statistics. In Debug, Internal and Profile builds each lock counts its acquisitions, the
// contended cases (TryEnter fails) and the total wait time; a Tracy zone marks the wait. Release
// without Tracy uses plain EnterCriticalSection. Output: Engine_LogCriticalSectionStats() at exit.
#if defined(_DEBUG) || defined(TRACY_ENABLE)
#define ENGINE_LOCK_STATS 1
#include <atomic>
#endif

class CriticalSection
{
	CRITICAL_SECTION m_windowsCriticalSection;
#ifdef ENGINE_LOCK_STATS
	const char *m_name;
	std::atomic<unsigned long long> m_acquisitions;
	std::atomic<unsigned long long> m_contended;
	std::atomic<unsigned long long> m_waitTicks;	///< QueryPerformanceCounter-Ticks
#endif
	public:
#ifdef ENGINE_LOCK_STATS
		explicit CriticalSection(const char *name = nullptr) : m_name(name), m_acquisitions(0), m_contended(0), m_waitTicks(0)
#else
		explicit CriticalSection(const char * = nullptr)
#endif
		{
			InitializeCriticalSection( &m_windowsCriticalSection );
		}

		virtual ~CriticalSection()
		{
			DeleteCriticalSection( &m_windowsCriticalSection );
		}

	public:	// Use these when entering/exiting a critical section.
		void enter( void ) 
		{ 
#ifdef ENGINE_LOCK_STATS
			m_acquisitions.fetch_add(1, std::memory_order_relaxed);
			if (TryEnterCriticalSection( &m_windowsCriticalSection ))
				return;
			m_contended.fetch_add(1, std::memory_order_relaxed);
			LARGE_INTEGER t0, t1;
			QueryPerformanceCounter(&t0);
			{
				PROF_ZONE("CriticalSection wait");
				EnterCriticalSection( &m_windowsCriticalSection );
			}
			QueryPerformanceCounter(&t1);
			m_waitTicks.fetch_add(static_cast<unsigned long long>(t1.QuadPart - t0.QuadPart), std::memory_order_relaxed);
#else
			EnterCriticalSection( &m_windowsCriticalSection );
#endif
		}
#ifdef ENGINE_LOCK_STATS
		const char *getName() const { return m_name ? m_name : "?"; }
		unsigned long long getAcquisitions() const { return m_acquisitions.load(std::memory_order_relaxed); }
		unsigned long long getContended() const { return m_contended.load(std::memory_order_relaxed); }
		unsigned long long getWaitTicks() const { return m_waitTicks.load(std::memory_order_relaxed); }
#endif
		
		void exit( void )
		{
			LeaveCriticalSection( &m_windowsCriticalSection );
		}
};

class ScopedCriticalSection
{
	private:
		CriticalSection *m_cs;
	
	public:
		ScopedCriticalSection( CriticalSection *cs ) : m_cs(cs)
		{ 
			if (m_cs) 
				m_cs->enter();
		}

		virtual ~ScopedCriticalSection( )
		{ 
			if (m_cs) 
				m_cs->exit();
		}
};

#include "mutex.h"

// Each lock is created on first use by a function (function-local static: thread-safe
// initialization, and it happens before any use, whichever static constructor calls first).
// This also covers tools and tests without WinMain and constructors of global objects that
// allocate from the memory pool. The objects are intentionally never destroyed: static
// destructors of other translation units still run afterwards and would otherwise use a
// destroyed CRITICAL_SECTION. The macros keep the old names valid for the call sites.
extern FastCriticalSectionClass TheAsciiStringCriticalSection;
CriticalSection *GetUnicodeStringCriticalSection( void );
CriticalSection *GetDmaCriticalSection( void );
CriticalSection *GetMemoryPoolCriticalSection( void );
CriticalSection *GetDebugLogCriticalSection( void );

#define TheUnicodeStringCriticalSection	(::GetUnicodeStringCriticalSection())
#define TheDmaCriticalSection			(::GetDmaCriticalSection())
#define TheMemoryPoolCriticalSection	(::GetMemoryPoolCriticalSection())
#define TheDebugLogCriticalSection		(::GetDebugLogCriticalSection())
/// Writes access/wait statistics of the four engine locks to the engine log (only with ENGINE_LOCK_STATS).
void Engine_LogCriticalSectionStats( void );

#endif /* __CRITICALSECTION_H__ */
