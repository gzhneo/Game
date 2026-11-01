/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RunAhead.h -- how many frames ahead commands are scheduled in a network game.
//
// A command given in frame F runs in frame F + run ahead on every machine, so the run ahead has to
// cover the time a command needs to reach the slowest player. Too little and the game stalls while
// it waits; too much and every click lags behind.
//
// LatencyEstimator follows the round-trip estimate of TCP (RFC 6298): a smoothed round trip and its
// mean deviation, so a line with jitter asks for more than its average. RunAheadController turns the
// round trip into frames and damps the changes: up at once (a stall costs more than a delay), down
// only after a few calm rounds and in steps, so the delay does not jump back and forth.
//
// No engine header: tested by EngineTests.

#pragma once

#ifndef ENGINE_RUNAHEAD_H
#define ENGINE_RUNAHEAD_H

namespace Engine
{

class LatencyEstimator
{
public:
	/// Round trips above this are measurement errors (a paused game), not the line.
	static constexpr float MAX_SAMPLE_SECONDS = 10.0f;

	void reset();
	/// One measured round trip in seconds; ignored if not finite, negative or above the maximum.
	void addSample(float seconds);

	bool hasSamples() const { return m_samples > 0; }
	float smoothed() const { return m_smoothed; }
	float deviation() const { return m_deviation; }
	/// A round trip most samples stay below: smoothed + 2 deviations; 0 without samples.
	float high() const { return m_smoothed + 2.0f * m_deviation; }

private:
	float m_smoothed = 0.0f;
	float m_deviation = 0.0f;
	unsigned m_samples = 0;
};

struct RunAheadLimits
{
	int minFrames = 6;
	int maxFrames = 64;
	int slackPercent = 10;		///< on top of the latency
	int safetyFrames = 2;		///< frame time and processing on both sides
};

class RunAheadController
{
public:
	/// Rounds with a lower target before the run ahead goes down.
	static constexpr int CALM_ROUNDS = 4;

	/// Frames a command needs over a path with this round trip at `fps`: the one-way time (half the
	/// round trip) in frames, rounded up, plus slack and safety, within the limits.
	static int target(float roundTripSeconds, int fps, const RunAheadLimits &limits);

	/// The run ahead to use from the current one and the new target.
	int next(int current, int target);
	void reset() { m_calmRounds = 0; }

private:
	int m_calmRounds = 0;
};

} // namespace Engine

#endif // ENGINE_RUNAHEAD_H
