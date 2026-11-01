/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RunAhead.cpp -- see RunAhead.h.

#include "Engine/RunAhead.h"

#include <cmath>

namespace Engine
{

void LatencyEstimator::reset()
{
	m_smoothed = 0.0f;
	m_deviation = 0.0f;
	m_samples = 0;
}

void LatencyEstimator::addSample(float seconds)
{
	if (!std::isfinite(seconds) || seconds < 0.0f || seconds > MAX_SAMPLE_SECONDS)
		return;
	if (m_samples == 0)
	{
		m_smoothed = seconds;
		m_deviation = seconds / 2.0f;
	}
	else
	{
		// RFC 6298 with alpha = 1/8, beta = 1/4
		m_deviation = 0.75f * m_deviation + 0.25f * std::fabs(m_smoothed - seconds);
		m_smoothed = 0.875f * m_smoothed + 0.125f * seconds;
	}
	if (m_samples < 0xFFFFFFFFu)
		++m_samples;
}

int RunAheadController::target(float roundTripSeconds, int fps, const RunAheadLimits &limits)
{
	if (!std::isfinite(roundTripSeconds) || roundTripSeconds < 0.0f)
		roundTripSeconds = 0.0f;
	if (roundTripSeconds > LatencyEstimator::MAX_SAMPLE_SECONDS)
		roundTripSeconds = LatencyEstimator::MAX_SAMPLE_SECONDS;
	if (fps < 1)
		fps = 1;
	const float oneWayFrames = roundTripSeconds / 2.0f * static_cast<float>(fps);
	int frames = static_cast<int>(std::ceil(oneWayFrames * (1.0f + static_cast<float>(limits.slackPercent) / 100.0f)));
	frames += limits.safetyFrames;
	if (frames < limits.minFrames)
		frames = limits.minFrames;
	if (frames > limits.maxFrames)
		frames = limits.maxFrames;
	return frames;
}

int RunAheadController::next(int current, int target)
{
	if (target >= current)
	{
		m_calmRounds = 0;
		return target;
	}
	if (++m_calmRounds < CALM_ROUNDS)
		return current;
	// Down in steps: half the distance, at least one frame.
	int step = (current - target) / 2;
	if (step < 1)
		step = 1;
	return current - step;
}

} // namespace Engine
