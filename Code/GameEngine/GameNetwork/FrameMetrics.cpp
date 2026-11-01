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

/** FrameMetrics.cpp */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/FrameMetrics.h"
#include "GameClient/Display.h"
#include "GameNetwork/networkutil.h"

FrameMetrics::FrameMetrics() 
{
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_averageFps = 0.0f;
	m_averageLatency = 0.0f;
	m_cushionIndex = 0;
	m_fpsListIndex = 0;
	m_lastFpsTimeThing = 0;
	m_minimumCushion = 0;

	m_pendingLatencies = std::make_unique<time_t[]>(MAX_FRAMES_AHEAD);
	for(Int i = 0; i < MAX_FRAMES_AHEAD; i++)
		m_pendingLatencies[i] = 0;
	//
	m_fpsList = std::make_unique<Real[]>(TheGlobalData->m_networkFPSHistoryLength);
	m_latencyList = std::make_unique<Real[]>(TheGlobalData->m_networkLatencyHistoryLength);
}

FrameMetrics::~FrameMetrics() {
	if (m_fpsList != nullptr) {
		m_fpsList = nullptr;
	}

	if (m_latencyList != nullptr) {
		m_latencyList = nullptr;
	}

	if (m_pendingLatencies)
	{
		m_pendingLatencies = nullptr;
	}
}

void FrameMetrics::init() {
	m_averageFps = 30;
	m_averageLatency = static_cast<Real>(0.2);
	m_latencyEstimator.reset();
	m_minimumCushion = -1;

	Int i;
	for (i = 0; static_cast<UnsignedInt>(i) < TheGlobalData->m_networkFPSHistoryLength; ++i) {
		m_fpsList[i] = 30.0;
	}
	m_fpsListIndex = 0;
	for (i = 0; static_cast<UnsignedInt>(i) < TheGlobalData->m_networkLatencyHistoryLength; ++i) {
		m_latencyList[i] = static_cast<Real>(0.2);
	}
	m_cushionIndex = 0;
}

void FrameMetrics::reset() {
	init();
}

void FrameMetrics::doPerFrameMetrics(UnsignedInt frame) {
	// Do the measurement of the fps.
	time_t curTime = timeGetTime();
	if ((curTime - m_lastFpsTimeThing) >= 1000) {
//		if ((m_fpsListIndex % 16) == 0) {
//			DEBUG_LOG(("FrameMetrics::doPerFrameMetrics - adding %f to fps history. average before: %f ", m_fpsList[m_fpsListIndex], m_averageFps));
//		}
		m_averageFps -= ((m_fpsList[m_fpsListIndex])) / TheGlobalData->m_networkFPSHistoryLength; // subtract out the old value from the average.
		// without a display (headless tests) the logic rate stands in
		m_fpsList[m_fpsListIndex] = TheDisplay != nullptr ? TheDisplay->getAverageFPS() : static_cast<Real>(LOGICFRAMES_PER_SECOND);
//		m_fpsList[m_fpsListIndex] = TheGameClient->getFrame() - m_fpsStartingFrame;
		m_averageFps += (static_cast<Real>(m_fpsList[m_fpsListIndex])) / TheGlobalData->m_networkFPSHistoryLength; // add the new value to the average.
//		DEBUG_LOG(("average after: %f\n", m_averageFps));
		++m_fpsListIndex;
		m_fpsListIndex %= TheGlobalData->m_networkFPSHistoryLength;
		m_lastFpsTimeThing = curTime;
	}

	Int pendingLatenciesIndex = frame % MAX_FRAMES_AHEAD;
	m_pendingLatencies[pendingLatenciesIndex] = curTime;

}

void FrameMetrics::processLatencyResponse(UnsignedInt frame) {
	time_t curTime = timeGetTime();
	Int pendingIndex = frame % MAX_FRAMES_AHEAD;
	time_t timeDiff = curTime - m_pendingLatencies[pendingIndex];

	Int latencyListIndex = frame % TheGlobalData->m_networkLatencyHistoryLength;
	m_averageLatency -= m_latencyList[latencyListIndex] / TheGlobalData->m_networkLatencyHistoryLength;
	m_latencyList[latencyListIndex] = static_cast<Real>(timeDiff) / static_cast<Real>(1000); // convert to seconds from milliseconds.
	m_averageLatency += m_latencyList[latencyListIndex] / TheGlobalData->m_networkLatencyHistoryLength;
	m_latencyEstimator.addSample(m_latencyList[latencyListIndex]);

	if (frame % 16 == 0) {
//		DEBUG_LOG(("ConnectionManager::processFrameInfoAck - average latency = %f\n", m_averageLatency));
	}
}

void FrameMetrics::addCushion(Int cushion) {
	++m_cushionIndex;
	m_cushionIndex %= TheGlobalData->m_networkCushionHistoryLength;
	if (m_cushionIndex == 0) {
		m_minimumCushion = -1;
	}
	if ((cushion < m_minimumCushion) || (m_minimumCushion == -1)) {
		m_minimumCushion = cushion;
	}
}

Int FrameMetrics::getAverageFPS() {
	return static_cast<Int>(m_averageFps);
}

Real FrameMetrics::getAverageLatency() {
	return m_averageLatency;
}

Real FrameMetrics::getHighLatency() {
	return m_latencyEstimator.hasSamples() ? m_latencyEstimator.high() : m_averageLatency;
}

Int FrameMetrics::getMinimumCushion() {
	return m_minimumCushion;
}