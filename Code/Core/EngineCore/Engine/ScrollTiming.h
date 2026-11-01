/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ScrollTiming.h -- the camera scrolls once per logic tick (render frames in between interpolate it).
// Scaling each step by the real time since the previous tick keeps the scroll speed the same at
// any game speed.
// The step itself is in screen units (W3DView::scrollBy turns it into ground distance).

#pragma once

#ifndef ENGINE_SCROLLTIMING_H
#define ENGINE_SCROLLTIMING_H

#include "Engine/Clock.h"

#include <cstdint>

namespace Engine
{

/// Longest gap between two ticks that still counts with its real length; matches the catch-up limit of
/// the main loop (GameEngine::execute), so every frame down to 4 per second scrolls at full speed.
constexpr std::uint64_t SCROLL_MAX_GAP_MICROSECONDS = 250000;

/// Factor for one scroll step: elapsed real time / nominal tick length. No lower and no further upper
/// limit: when several ticks run back to back after a slow frame, the first one carries the whole frame
/// time and the others almost nothing, so the sum matches real time at any frame rate. The first tick
/// (elapsed 0) and longer gaps (pause, loading, window dragged) count as one nominal tick, so the camera
/// never jumps.
inline double scrollTickScale(std::uint64_t elapsedMicroseconds)
{
	const double nominal = 1000000.0 * LogicTime::SECONDS_PER_FRAME;
	if (elapsedMicroseconds == 0 || elapsedMicroseconds > SCROLL_MAX_GAP_MICROSECONDS)
		return 1.0;
	return static_cast<double>(elapsedMicroseconds) / nominal;
}

/// Speed setting (slider / 100, limited to 0..1) to speed: (s + 2 s^3) / 3. Gives 0 at 0, a quarter at the
/// middle and 1 at the top; the linear part keeps small settings usable. Same curve for key, edge and drag.
inline double scrollSpeedCurve(double setting)
{
	const double s = setting < 0.0 ? 0.0 : (setting > 1.0 ? 1.0 : setting);
	return (s + 2.0 * s * s * s) / 3.0;
}

/// Curve value at which the drag runs at its plain distance term (axis factor per pixel).
constexpr double SCROLL_DRAG_REFERENCE_SPEED = 0.5;

/// Key and edge scrolling: step per nominal tick along one axis (axis factor from GameData.ini, fixed amount,
/// speed setting 0..1).
inline double scrollKeyStep(double axisFactor, double amount, double setting)
{
	return axisFactor * amount * scrollSpeedCurve(setting);
}

/// Right-button dragging along one axis: the cursor's distance from the anchor in pixels times the axis factor,
/// scaled by the speed curve like the other kinds, plus a minimum of axisFactor * curve^2 along the drag
/// direction (component of the unit vector).
inline double scrollDragStep(double axisFactor, double distance, double direction, double setting)
{
	const double speed = scrollSpeedCurve(setting);
	return axisFactor * distance * (speed / SCROLL_DRAG_REFERENCE_SPEED) + axisFactor * direction * speed * speed;
}

}	// namespace Engine

#endif	// ENGINE_SCROLLTIMING_H
