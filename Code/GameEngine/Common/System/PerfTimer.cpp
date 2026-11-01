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

// FILE: PerfTimer.cpp ///////////////////////////////////////////////////////////////////////////
// Author: 
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/PerfTimer.h"

#include "Common/GlobalData.h"
#include "GameClient/DebugDisplay.h"
#include "GameClient/Display.h"

#include <intrin.h>	// __rdtsc

// __rdtsc() intrinsic; x64 has no inline assembler.
__forceinline void ProfileGetTime(__int64 &t)
{
	t = (__int64)__rdtsc();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

#ifdef DUMP_PERF_STATS

//-------------------------------------------------------------------------------------------------
static Int64 s_ticksPerSec = 0;
static double s_ticksPerMSec = 0;
static double s_ticksPerUSec = 0;

//-------------------------------------------------------------------------------------------------
void GetPrecisionTimerTicksPerSec(Int64* t)
{
	*t = s_ticksPerSec;
}

//Kris: Plugged in Martin's code to optimize timer setup.
#define HOFFESOMMER_REPLACEMENT_CODE

//-------------------------------------------------------------------------------------------------
void InitPrecisionTimer()
{
#ifdef HOFFESOMMER_REPLACEMENT_CODE

  // measure clock cycles 3 times for 20 msec each
  // then take the 2 counts that are closest, average
  Int64 n[ 3 ];
  for( int k = 0; k < 3; k++ )
  {
    // wait for end of current tick
    unsigned timeEnd = timeGetTime() + 2;
    while( timeGetTime() < timeEnd ); //do nothing
 
    // get cycles
    Int64 start, startQPC, endQPC;
    QueryPerformanceCounter( (LARGE_INTEGER *)&startQPC );
    ProfileGetTime( start );
    timeEnd += 20;
    while( timeGetTime() < timeEnd ); //do nothing
    ProfileGetTime( n[ k ] );
    n[ k ] -= start;
 
    // convert to 1 second
    if( QueryPerformanceCounter( (LARGE_INTEGER*)&endQPC ) )
    {
      QueryPerformanceFrequency( (LARGE_INTEGER*)&s_ticksPerSec );
      n[ k ] = ( n[ k ] * s_ticksPerSec ) / ( endQPC - startQPC );
    }
    else
    {
      n[ k ] = ( n[ k ] * 1000 ) / 20;
    }
  }
 
  // find two closest values
  Int64 d01 = n[ 1 ] - n[ 0 ];
	Int64 d02 = n[ 2 ] - n[ 0 ];
	Int64 d12 = n[ 2 ] - n[ 1 ];

  if( d01 < 0 )
	{
		d01 = -d01;
	}
  if( d02 < 0 ) 
	{
		d02 = -d02;
	}
  if( d12 < 0 )
	{
		d12 = -d12;
	}

  Int64 avg;
  if( d01 < d02 )
  {
    avg = d01 < d12 ? n[ 0 ] + n[ 1 ] : n[ 1 ] + n[ 2 ];
  }
  else
  {
    avg = d02 < d12 ? n[ 0 ] + n[ 2 ] : n[ 1 ] + n[ 2 ];
  }

	//s_ticksPerMSec = 1.0 * TotalTicks / totalTime;
	s_ticksPerMSec = avg / 2000.0f;
	s_ticksPerSec = static_cast<Int64>(s_ticksPerMSec * 1000.0f);
	s_ticksPerUSec = s_ticksPerSec / 1000000.0f;

	
#else

	//Kris: With total disrespect, this code costs 5 real seconds of init time
	//whenever we fire up the game.

	#ifdef USE_QPF
		QueryPerformanceFrequency((LARGE_INTEGER*)&s_ticksPerSec);
	#else
		// Init the precision timers
		Int64 totalTime = 0;
		Int64	TotalTicks = 0;
		static int TESTS = 5;
		
		for (int i = 0; i < TESTS; ++i) 
		{
			int        TimeStart;
			int        TimeStop;
			Int64		   StartTicks;
			Int64		   EndTicks;

			TimeStart = timeGetTime();
			GetPrecisionTimer(&StartTicks);
			for(;;)
			{
				TimeStop = timeGetTime();
				if ((TimeStop - TimeStart) > 1000)
				{
					GetPrecisionTimer(&EndTicks);
					break;
				}
			}

			TotalTicks += (EndTicks - StartTicks);

			totalTime += (TimeStop - TimeStart);
		}

		s_ticksPerMSec = 1.0 * TotalTicks / totalTime;
		s_ticksPerSec = s_ticksPerMSec * 1000.0f;
	#endif
		s_ticksPerMSec = s_ticksPerSec / 1000.0f;
		s_ticksPerUSec = s_ticksPerSec / 1000000.0f;

#endif

}
#endif // DUMP_PERF_STATS

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------


