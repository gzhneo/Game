/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WheelSteps.h -- whole wheel notches from wheel deltas. High-resolution wheels and touchpads report
// deltas below one notch (WHEEL_DELTA, 120); the rest is kept for the next event instead of being
// lost. A change of direction drops the rest, so turning back responds at once.

#pragma once

#ifndef ENGINE_WHEELSTEPS_H
#define ENGINE_WHEELSTEPS_H

namespace Engine
{

const int WHEEL_NOTCH = 120;

/// Adds delta to remainder and returns the whole notches it now holds (negative = towards the user).
inline int takeWheelNotches(int &remainder, int delta)
{
	if ((delta > 0 && remainder < 0) || (delta < 0 && remainder > 0))
		remainder = 0;
	remainder += delta;
	const int notches = remainder / WHEEL_NOTCH;	// truncates towards zero
	remainder -= notches * WHEEL_NOTCH;
	return notches;
}

}	// namespace Engine

#endif	// ENGINE_WHEELSTEPS_H
