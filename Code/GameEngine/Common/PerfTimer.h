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

// PerfTimer.h ////////////////////////////////////////////////////////////////////////////////////
// John McDonald
// July 2002

#pragma once

#ifndef __PERFTIMER_H__
#define __PERFTIMER_H__

#include "Common/GameCommon.h"	// ensure we get DUMP_PERF_STATS, or not
// The PROF_* macros must be available everywhere because GameLogic/GameEngine use them directly.
// Profiling uses Tracy; USE_PERF_TIMER sites become Tracy zones. The precision timer under
// DUMP_PERF_STATS is for debug statistics.
#include "wwtracy.h"


// Forward Declarations
class DebugDisplayInterface;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

#define NO_USE_QPF	// non-QPF is much faster.

#ifdef DUMP_PERF_STATS
//-------------------------------------------------------------------------------------------------
void InitPrecisionTimer();

//-------------------------------------------------------------------------------------------------
void GetPrecisionTimerTicksPerSec(Int64* t);

//-------------------------------------------------------------------------------------------------
__forceinline void GetPrecisionTimer(Int64* t)
{
#ifdef USE_QPF
	QueryPerformanceCounter((LARGE_INTEGER*)t);
#else
	// CPUID is needed to force serialization of any previous instructions.
	// (for now, I am commenting this out. It throws the timings off a bit more (up to .001%) jkmcd)
	*t = (Int64)__rdtsc();
#endif
}
#endif

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------


	// USE_PERF_TIMER sites become Tracy zones in the Profile build (name = timer id). The semicolon
	// is part of the macro because some callers omit it.
	#define DECLARE_PERF_TIMER(id)
	#define  DECLARE_TOTAL_PERF_TIMER(id)
	#ifdef TRACY_ENABLE
		#define USE_PERF_TIMER(id)						PROF_ZONE(#id);
	#else
		#define USE_PERF_TIMER(id)
	#endif
	#define IGNORE_PERF_TIMER(id)


#endif /* __PERFTIMER_H__ */
