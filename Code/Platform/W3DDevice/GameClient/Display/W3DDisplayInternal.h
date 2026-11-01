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

// W3DDisplayInternal.h -- what the W3DDisplay*.cpp files share and nobody else needs.

#pragma once

// Not the ones from PerfTimer.h: those are only valid with "-vtune".
inline Int64 getPerformanceCounter()
{
	Int64 tmp;
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&tmp));
	return tmp;
}

inline Int64 getPerformanceCounterFrequency()
{
	Int64 tmp;
	QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&tmp));
	return tmp;
}

/// The cumulative frame rate leaves out the first half second of a game.
inline constexpr UnsignedInt START_CUMU_FRAME = LOGICFRAMES_PER_SECOND / 2;

#if defined(_DEBUG)
/// The mouse position for picking the object under the cursor; WindowXlat sets it.
extern ICoord2D TheMousePos;
#endif
